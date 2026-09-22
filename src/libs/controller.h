#ifndef EMULATOR_INCLUDE_EMULATOR_CONTROLLER_H_
#define EMULATOR_INCLUDE_EMULATOR_CONTROLLER_H_

#include "common/abi.h"
#include "common/common.h"
namespace Libs::Controller {

void Initialize();
void Shutdown();
void EmergencyShutdown();

struct Lifecycle {
	static constexpr const char* name               = "Controller";
	static constexpr auto        initialize         = Libs::Controller::Initialize;
	static constexpr auto        shutdown           = Libs::Controller::Shutdown;
	static constexpr auto        emergency_shutdown = Libs::Controller::EmergencyShutdown;
};

constexpr int HOST_INPUT_CONTROLLER_ID = -1000;

constexpr uint32_t PAD_BUTTON_L3        = 0x00000002;
constexpr uint32_t PAD_BUTTON_R3        = 0x00000004;
constexpr uint32_t PAD_BUTTON_OPTIONS   = 0x00000008;
constexpr uint32_t PAD_BUTTON_UP        = 0x00000010;
constexpr uint32_t PAD_BUTTON_RIGHT     = 0x00000020;
constexpr uint32_t PAD_BUTTON_DOWN      = 0x00000040;
constexpr uint32_t PAD_BUTTON_LEFT      = 0x00000080;
constexpr uint32_t PAD_BUTTON_L2        = 0x00000100;
constexpr uint32_t PAD_BUTTON_R2        = 0x00000200;
constexpr uint32_t PAD_BUTTON_L1        = 0x00000400;
constexpr uint32_t PAD_BUTTON_R1        = 0x00000800;
constexpr uint32_t PAD_BUTTON_TRIANGLE  = 0x00001000;
constexpr uint32_t PAD_BUTTON_CIRCLE    = 0x00002000;
constexpr uint32_t PAD_BUTTON_CROSS     = 0x00004000;
constexpr uint32_t PAD_BUTTON_SQUARE    = 0x00008000;
constexpr uint32_t PAD_BUTTON_TOUCH_PAD = 0x00100000;
constexpr uint32_t PAD_BUTTON_SHARE     = 0x00000001;

enum class Axis {
	LeftX        = 0,
	LeftY        = 1,
	RightX       = 2,
	RightY       = 3,
	TriggerLeft  = 4,
	TriggerRight = 5,

	AxisMax
};

enum class Sensor { Accel, Gyro };

struct PadControllerInformation;
struct PadData;
struct PadVibrationParam;
struct PadLightBarParam;
struct PadTriggerEffectParam;

inline int controller_get_axis(int min, int max, int value) {
	int v = (255 * (value - min)) / (max - min);
	if (v < 0) {
		return 0;
	}
	if (v > 255) {
		return 255;
	}
	return v;
}

void Connect(int id);
void Disconnect(int id);
void SetButton(int id, uint32_t button, bool down);
void SetAxis(int id, Axis axis, int value);
void SetRightStick(int id, int x, int y);
void SetTouchPad(int id, int finger, bool down, float x, float y);
void SetSensor(int id, Sensor sensor, const float* data, uint64_t time_us);
void ResetInputState();

// Magnus iOS port-based API (src/ios/embed.cpp). Kyty upstream uses id-based
// Connect/SetButton above; iOS front-end uses ports 0..PAD_PORT_MAX-1.
// Implemented in src/ios/controller_shim.cpp (iOS only, TODO: wire to id API).
constexpr int PAD_PORT_MAX     = 4;
constexpr int PAD_USER_ID_BASE = 1000;
int  PortForUserId(int user_id);
int  UserIdForPort(int port);
void ControllerConnect(int port, int id);
void ControllerDisconnect(int port, int id);
void ControllerButton(int port, int id, uint32_t button, bool down);
void ControllerAxis(int port, int id, Axis axis, int value);
void ControllerTouch(int port, int id, bool down, uint16_t x, uint16_t y);
void ControllerResetInputState();
void ControllerSetPortConnected(int port, bool connected);
bool ControllerPortConnected(int port);
int  ControllerConnectedPortCount();

int KYTY_SYSV_ABI PadInit();
int KYTY_SYSV_ABI PadOpen(int user_id, int type, int index, const void* param);
int KYTY_SYSV_ABI PadGetHandle(int user_id, int type, int index);
int KYTY_SYSV_ABI PadSetMotionSensorState(int handle, bool enable);
int KYTY_SYSV_ABI PadSetAngularVelocityDeadbandState(int handle, bool enable);
int KYTY_SYSV_ABI PadResetOrientation(int handle);
int KYTY_SYSV_ABI PadGetControllerInformation(int handle, PadControllerInformation* info);
int KYTY_SYSV_ABI PadIsRemoteController(int handle, bool* is_remote);
int KYTY_SYSV_ABI PadReadState(int handle, PadData* data);
int KYTY_SYSV_ABI PadRead(int handle, PadData* data, int num);
int KYTY_SYSV_ABI PadSetVibration(int handle, const PadVibrationParam* param);
int KYTY_SYSV_ABI PadSetTriggerEffect(int handle, const PadTriggerEffectParam* param);
int KYTY_SYSV_ABI PadResetLightBar(int handle);
int KYTY_SYSV_ABI PadSetLightBar(int handle, const PadLightBarParam* param);

} // namespace Libs::Controller

#endif /* EMULATOR_INCLUDE_EMULATOR_CONTROLLER_H_ */
