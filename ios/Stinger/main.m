// MagnusPS5 Stinger UI — programmatic UIKit, Objective-C only.
// No Swift, no storyboards/XIBs, no asset catalogs. Links the core static
// lib (src/ios/embed.h) and ships in the unsigned IPA for user signing.
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#import <UIKit/UIKit.h>

#include "ios/embed.h"

static NSString* MagnusGamesDirectory(void) {
	NSArray* docs =
      [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                           inDomains:NSUserDomainMask];
	NSString* games =
      [[docs.firstObject path] stringByAppendingPathComponent:@"Games"];
	[NSFileManager.defaultManager createDirectoryAtPath:games
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
	return games;
}

static BOOL MagnusFolderHasEboot(NSString* folder) {
	return [NSFileManager.defaultManager
      fileExistsAtPath:[folder stringByAppendingPathComponent:@"eboot.bin"]];
}

@interface MagnusGameViewController : UIViewController
- (instancetype)initWithGameFolder:(NSString*)folder;
@end

@implementation MagnusGameViewController {
	NSString* _folder;
	UILabel* _status;
	NSTimer* _timer;
	UIView* _metalView;
}

- (instancetype)initWithGameFolder:(NSString*)folder {
	if ((self = [super init])) {
		_folder = [folder copy];
	}
	return self;
}

- (void)viewDidLoad {
	[super viewDidLoad];
	self.title = [_folder lastPathComponent];
	self.view.backgroundColor = [UIColor systemBackgroundColor];

	_metalView = [[UIView alloc] init];
	_metalView.translatesAutoresizingMaskIntoConstraints = NO;
	_metalView.backgroundColor = [UIColor blackColor];
	[self.view addSubview:_metalView];

	_status = [[UILabel alloc] init];
	_status.translatesAutoresizingMaskIntoConstraints = NO;
	_status.numberOfLines = 0;
	_status.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
	_status.text = @"starting…";
	[self.view addSubview:_status];

	[NSLayoutConstraint activateConstraints:@[
    [_metalView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
    [_metalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [_metalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
    [_metalView.heightAnchor constraintEqualToAnchor:_metalView.widthAnchor multiplier:9.0 / 16.0],
    [_status.topAnchor constraintEqualToAnchor:_metalView.bottomAnchor constant:12],
    [_status.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:16],
    [_status.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-16],
  ]];

	// A19 Pro defaults: 60Hz vblank, spatial MetalFX, lowest internal res,
	// persistent shader cache. See docs/IPHONE17PROMAX_PERF.md.
	NSArray* caches =
      [NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory
                                                   inDomains:NSUserDomainMask];
	NSString* shaderCache =
      [[caches.firstObject path] stringByAppendingPathComponent:@"MagnusPS5/shader_cache"];
	[NSFileManager.defaultManager createDirectoryAtPath:shaderCache
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
	magnus_set_shader_cache_dir([shaderCache UTF8String]);
	magnus_set_vblank_frequency(60);
	magnus_set_metal_fx(true, false, false);
	magnus_set_screen_size(0);
	magnus_set_volume(100);
	magnus_set_network_enabled(false);

	if (!magnus_boot_game([_folder UTF8String])) {
		const char* why = magnus_boot_failure();
		_status.text = [NSString stringWithFormat:@"boot refused: %s",
                                                  why ? why : "unknown"];
		return;
	}
	__weak typeof(self) weakSelf = self;
	_timer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                           repeats:YES
                                             block:^(NSTimer* t) {
    (void)t;
    __strong typeof(self) s = weakSelf;
    if (!s) {
      return;
    }
    [s refreshStatus];
  }];
}

- (void)viewDidLayoutSubviews {
	[super viewDidLayoutSubviews];
	CALayer* layer = _metalView.layer;
	magnus_set_surface((__bridge void*)layer, (uint32_t)_metalView.bounds.size.width,
                     (uint32_t)_metalView.bounds.size.height);
}

- (void)refreshStatus {
	struct MagnusStats st = {0};
	magnus_stats(&st);
	const char* failure = magnus_boot_failure();
	_status.text = [NSString
      stringWithFormat:@"state=%d frames=%llu\nboot: %s\ncompiled=%llu "
                       @"shader_us=%llu pipelines=%llu",
                       magnus_state(), magnus_guest_frames(),
                       failure ? failure : "(running)",
                       st.compiled_blocks, st.shader_compile_us, st.pipelines];
}

- (void)viewWillDisappear:(BOOL)animated {
	[super viewWillDisappear:animated];
	magnus_set_paused(true);
	[_timer invalidate];
	_timer = nil;
}

@end

@interface MagnusLibraryViewController
    : UITableViewController <UIDocumentPickerDelegate>
@end

@implementation MagnusLibraryViewController {
	NSArray<NSString*>* _games;
}

- (void)viewDidLoad {
	[super viewDidLoad];
	self.title = @"MagnusPS5";
	self.navigationItem.rightBarButtonItems = @[
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAdd
                                                  target:self
                                                  action:@selector(importFolder)],
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                  target:self
                                                  action:@selector(rescan)],
  ];
	[self rescan];
}

- (void)rescan {
	NSString* root = MagnusGamesDirectory();
	NSArray* names = [NSFileManager.defaultManager contentsOfDirectoryAtPath:root
                                                                     error:nil];
	NSMutableArray* found = [NSMutableArray array];
	for (NSString* name in names) {
		NSString* full = [root stringByAppendingPathComponent:name];
		BOOL isDir = NO;
		if ([NSFileManager.defaultManager fileExistsAtPath:full isDirectory:&isDir] &&
        isDir) {
			[found addObject:full];
		}
	}
	_games = [found sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
	[self.tableView reloadData];
}

- (void)importFolder {
	UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
      initWithDocumentTypes:@[@"public.folder"]
                     inMode:UIDocumentPickerModeOpen];
	picker.delegate = self;
	picker.allowsMultipleSelection = NO;
	[self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
	(void)controller;
	NSURL* url = urls.firstObject;
	if (!url) {
		return;
	}
	BOOL access = [url startAccessingSecurityScopedResource];
	NSString* dest =
      [MagnusGamesDirectory() stringByAppendingPathComponent:url.lastPathComponent];
	[[NSFileManager defaultManager] removeItemAtPath:dest error:nil];
	NSError* err = nil;
	[[NSFileManager defaultManager] copyItemAtURL:url
                                            toURL:[NSURL fileURLWithPath:dest]
                                            error:&err];
	if (access) {
		[url stopAccessingSecurityScopedResource];
	}
	if (err) {
		UIAlertController* alert = [UIAlertController
          alertControllerWithTitle:@"Import failed"
                           message:err.localizedDescription
                    preferredStyle:UIAlertControllerStyleAlert];
		[alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                                  style:UIAlertActionStyleDefault
                                                handler:nil]];
		[self presentViewController:alert animated:YES completion:nil];
		return;
	}
	[self rescan];
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
	(void)tableView;
	(void)section;
	return _games.count;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
	UITableViewCell* cell =
      [tableView dequeueReusableCellWithIdentifier:@"game"] ?:
      [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                             reuseIdentifier:@"game"];
	NSString* folder = _games[indexPath.row];
	cell.textLabel.text = [folder lastPathComponent];
	cell.detailTextLabel.text =
      MagnusFolderHasEboot(folder) ? @"eboot.bin present" : @"missing eboot.bin";
	cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
	return cell;
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
	[tableView deselectRowAtIndexPath:indexPath animated:YES];
	MagnusGameViewController* game = [[MagnusGameViewController alloc]
      initWithGameFolder:_games[indexPath.row]];
	[self.navigationController pushViewController:game animated:YES];
}

@end

@interface MagnusAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation MagnusAppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
	(void)application;
	(void)launchOptions;
	self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
	MagnusLibraryViewController* library = [[MagnusLibraryViewController alloc] init];
	UINavigationController* nav =
      [[UINavigationController alloc] initWithRootViewController:library];
	self.window.rootViewController = nav;
	[self.window makeKeyAndVisible];
	return YES;
}
@end

int main(int argc, char* argv[]) {
	@autoreleasepool {
		return UIApplicationMain(argc, argv, nil, NSStringFromClass([MagnusAppDelegate class]));
	}
}
