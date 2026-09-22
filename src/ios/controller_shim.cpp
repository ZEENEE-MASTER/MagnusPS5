// Magnus iOS shims for Kyty Sep-20 base (iPhone 17 Pro Max).
// Upstream Kyty uses id-based controller/audio/network APIs; Magnus iOS
// front-end (src/ios/embed.cpp) uses port-based + master-volume + network
// kill-switch. These stubs allow the iOS static lib to link; TODO is to wire
// them to the id-based implementations after the iOS Actions build is green.
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#include "libs/audio.h"
#include "libs/controller.h"
#include "libs/network.h"

#include <atomic>
#include <cstdint>

namespace Libs::Audio {
namespace {
std::atomic<int> g_master_volume {100};
} // namespace

void SetMasterVolume(int percent) {
	if (percent < 0) {
		percent = 0;
	}
	if (percent > 100) {
		percent = 100;
	}
	g_master_volume.store(percent, std::memory_order_relaxed);
	// TODO: propagate to AudioOut before next AudioOutOutput().
}

namespace AudioIn {
void CapturePush(const int16_t* /*frames*/, uint32_t /*count*/) {
	// TODO: ring-buffer iPhone mic frames for AudioInInput().
}

void CaptureReset() {
	// TODO: clear mic ring-buffer.
}
} // namespace AudioIn
} // namespace Libs::Audio

namespace Libs::Network::NetCtl {
namespace {
std::atomic_bool g_network_enabled {false};
} // namespace

void SetNetworkEnabled(bool enabled) {
	g_network_enabled.store(enabled, std::memory_order_relaxed);
	// TODO: gate NetCtlGetState/Info on g_network_enabled for A19 Pro battery.
}
} // namespace Libs::Network::NetCtl

namespace Libs::Controller {
namespace {
std::atomic_bool g_port_connected[PAD_PORT_MAX] {};
} // namespace

int PortForUserId(int user_id) {
	const int port = user_id - PAD_USER_ID_BASE;
	return (port >= 0 && port < PAD_PORT_MAX) ? port : -1;
}

int UserIdForPort(int port) {
	return (port >= 0 && port < PAD_PORT_MAX) ? (PAD_USER_ID_BASE + port) : -1;
}

void ControllerConnect(int port, int id) {
	(void)port;
	Connect(id);
}

void ControllerDisconnect(int port, int id) {
	(void)port;
	Disconnect(id);
}

void ControllerButton(int port, int id, uint32_t button, bool down) {
	(void)port;
	SetButton(id, button, down);
}

void ControllerAxis(int port, int id, Axis axis, int value) {
	(void)port;
	SetAxis(id, axis, value);
}

void ControllerTouch(int port, int id, bool down, uint16_t x, uint16_t y) {
	// Kyty SetTouchPad takes (id, finger, down, x, y) with float 0..1.
	// Magnus passes 0..1920/943; normalize here until Stinger sends 0..1.
	const float fx = static_cast<float>(x) / 1920.0f;
	const float fy = static_cast<float>(y) / 943.0f;
	(void)port;
	SetTouchPad(id, 0, down, fx, fy);
}

void ControllerResetInputState() {
	ResetInputState();
}

void ControllerSetPortConnected(int port, bool connected) {
	if (port < 0 || port >= PAD_PORT_MAX) {
		return;
	}
	g_port_connected[port].store(connected, std::memory_order_relaxed);
	const int id = UserIdForPort(port);
	if (connected) {
		Connect(id);
	} else {
		Disconnect(id);
	}
}

bool ControllerPortConnected(int port) {
	if (port < 0 || port >= PAD_PORT_MAX) {
		return false;
	}
	return g_port_connected[port].load(std::memory_order_relaxed);
}

int ControllerConnectedPortCount() {
	int n = 0;
	for (int i = 0; i < PAD_PORT_MAX; i++) {
		if (g_port_connected[i].load(std::memory_order_relaxed)) {
			n++;
		}
	}
	return n;
}
} // namespace Libs::Controller
