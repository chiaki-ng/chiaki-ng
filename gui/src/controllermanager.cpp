// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <controllermanager.h>

#include <QCoreApplication>
#include <QByteArray>
#include <QTimer>

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
#include <SDL.h>
#endif

// SDL renamed the joystick power-level getter in 2.24.0; map to the correct
// symbol based on the compiled SDL version.
#if SDL_VERSION_ATLEAST(2, 24, 0)
#define CHIAKI_SDL_JOYSTICK_GET_POWERLEVEL(js) SDL_JoystickCurrentPowerLevel(js)
#else
#define CHIAKI_SDL_JOYSTICK_GET_POWERLEVEL(js) SDL_JoystickGetPowerLevel(js)
#endif

/* PS5 trigger effect documentation:
   https://controllers.fandom.com/wiki/Sony_DualSense#FFB_Trigger_Modes

   Taken from SDL2, licensed under the zlib license,
   Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
   https://github.com/libsdl-org/SDL/blob/release-2.24.1/test/testgamecontroller.c#L263-L289
*/
typedef struct
{
    Uint8 ucEnableBits1;                /* 0 */
    Uint8 ucEnableBits2;                /* 1 */
    Uint8 ucRumbleRight;                /* 2 */
    Uint8 ucRumbleLeft;                 /* 3 */
    Uint8 ucHeadphoneVolume;            /* 4 */
    Uint8 ucSpeakerVolume;              /* 5 */
    Uint8 ucMicrophoneVolume;           /* 6 */
    Uint8 ucAudioEnableBits;            /* 7 */
    Uint8 ucMicLightMode;               /* 8 */
    Uint8 ucAudioMuteBits;              /* 9 */
    Uint8 rgucRightTriggerEffect[11];   /* 10 */
    Uint8 rgucLeftTriggerEffect[11];    /* 21 */
    Uint8 rgucUnknown1[6];              /* 32 */
    Uint8 ucEnableBits3;                /* 38 */
    Uint8 rgucUnknown2[2];              /* 39 */
    Uint8 ucLedAnim;                    /* 41 */
    Uint8 ucLedBrightness;              /* 42 */
    Uint8 ucPadLights;                  /* 43 */
    Uint8 ucLedRed;                     /* 44 */
    Uint8 ucLedGreen;                   /* 45 */
    Uint8 ucLedBlue;                    /* 46 */
} DS5EffectsState_t;

static QSet<QString> chiaki_motion_controller_guids({
	// Sony on Linux
	"03000000341a00003608000011010000",
	"030000004c0500006802000010010000",
	"030000004c0500006802000010810000",
	"030000004c0500006802000011010000",
	"030000004c0500006802000011810000",
	"030000006f0e00001402000011010000",
	"030000008f0e00000300000010010000",
	"050000004c0500006802000000010000",
	"050000004c0500006802000000800000",
	"050000004c0500006802000000810000",
	"05000000504c415953544154494f4e00",
	"060000004c0500006802000000010000",
	"030000004c050000a00b000011010000",
	"030000004c050000a00b000011810000",
	"030000004c050000c405000011010000",
	"030000004c050000c405000011810000",
	"030000004c050000cc09000000010000",
	"030000004c050000cc09000011010000",
	"030000004c050000cc09000011810000",
	"03000000c01100000140000011010000",
	"050000004c050000c405000000010000",
	"050000004c050000c405000000810000",
	"050000004c050000c405000001800000",
	"050000004c050000cc09000000010000",
	"050000004c050000cc09000000810000",
	"050000004c050000cc09000001800000",
	// Sony on iOS
	"050000004c050000cc090000df070000",
	// Sony on Android
	"050000004c05000068020000dfff3f00",
	"030000004c050000cc09000000006800",
	"050000004c050000c4050000fffe3f00",
	"050000004c050000cc090000fffe3f00",
	"050000004c050000cc090000ffff3f00",
	"35643031303033326130316330353564",
	// Sony on Mac OSx
	"030000004c050000cc09000000000000",
	"030000004c0500006802000000000000",
	"030000004c0500006802000000010000",
	"030000004c050000a00b000000010000",
	"030000004c050000c405000000000000",
	"030000004c050000c405000000010000",
	"030000004c050000cc09000000010000",
	"03000000c01100000140000000010000",
	// Sony on Windows
	"030000004c050000a00b000000000000",
	"030000004c050000c405000000000000",
	"030000004c050000cc09000000000000",
	"03000000250900000500000000000000",
	"030000004c0500006802000000000000",
	"03000000632500007505000000000000",
	"03000000888800000803000000000000",
	"030000008f0e00001431000000000000",
});

static QSet<QPair<uint16_t, uint16_t>> chiaki_dualsense_controller_ids({
	// in format (vendor id, product id)
	QPair<uint16_t, uint16_t>(0x054c, 0x0ce6), // DualSense controller
});

static QSet<QPair<uint16_t, uint16_t>> chiaki_dualsense_edge_controller_ids({
	// in format (vendor id, product id)
	QPair<uint16_t, uint16_t>(0x054c, 0x0df2), // DualSense Edge controller
});

static QSet<QPair<uint16_t, uint16_t>> chiaki_handheld_controller_ids({
	// in format (vendor id, product id)
	QPair<uint16_t, uint16_t>(0x28de, 0x1205), // Steam Deck
	QPair<uint16_t, uint16_t>(0x0b05, 0x1abe), // Rog Ally
	QPair<uint16_t, uint16_t>(0x17ef, 0x6182), // Legion Go
	QPair<uint16_t, uint16_t>(0x0db0, 0x1901), // MSI Claw
});

static QSet<QPair<uint16_t, uint16_t>> chiaki_steam_virtual_controller_ids({
	// in format (vendor id, product id)
#ifdef Q_OS_MACOS
    QPair<uint16_t, uint16_t>(0x045e, 0x028e), // Microsoft Xbox 360 Controller
#else
	QPair<uint16_t, uint16_t>(0x28de, 0x11ff), // Steam Virtual Controller
#endif
});

static ControllerManager *instance = nullptr;

#define UPDATE_INTERVAL_MS 4
#define MOVE_CHECK_MS 1000

ControllerManager *ControllerManager::GetInstance()
{
	if(!instance)
		instance = new ControllerManager(qApp);
	return instance;
}

ControllerManager::ControllerManager(QObject *parent)
	: QObject(parent), creating_controller_mapping(false),
	joystick_allow_background_events(true), dualsense_intensity(0x00), ds5_gyro_fix_enabled(false), is_app_active(true)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	SDL_SetMainReady();
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#if SDL_VERSION_ATLEAST(2, 29, 1)
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "0");
#endif
	if(SDL_Init(SDL_INIT_GAMECONTROLLER) < 0)
		return;

	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &ControllerManager::HandleEvents);
	timer->start(UPDATE_INTERVAL_MS);
	auto move_timer = new QTimer(this);
	connect(move_timer, &QTimer::timeout, this, &ControllerManager::CheckMoved);
	move_timer->start(MOVE_CHECK_MS);
#endif

	UpdateAvailableControllers();
}

ControllerManager::~ControllerManager()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	open_controllers.clear();
	SDL_Quit();
#endif
}

void ControllerManager::SetAllowJoystickBackgroundEvents(bool enabled)
{
	this->joystick_allow_background_events = enabled;
}

void ControllerManager::CheckMoved()
{
	if(this->moved)
	{
		this->moved = false;
		emit ControllerMoved();
	}
}

void ControllerManager::SetIsAppActive(bool active)
{
	this->is_app_active = active;
}

void ControllerManager::SetButtonsByPos()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
#endif
}

void ControllerManager::UpdateAvailableControllers()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	QSet<SDL_JoystickID> current_controllers;
	for(int i=0; i<SDL_NumJoysticks(); i++)
	{
		if(!SDL_IsGameController(i))
			continue;

		// We'll try to identify pads with Motion Control
		SDL_JoystickGUID guid = SDL_JoystickGetGUID(SDL_JoystickOpen(i));
		char guid_str[256];
		SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));

		if(chiaki_motion_controller_guids.contains(guid_str))
		{
			SDL_Joystick *joy = SDL_JoystickOpen(i);
			if(joy)
			{
				bool no_buttons = SDL_JoystickNumButtons(joy) == 0;
				SDL_JoystickClose(joy);
				if(no_buttons)
					continue;
			}
		}

		current_controllers.insert(SDL_JoystickGetDeviceInstanceID(i));
	}

	if(current_controllers != available_controllers)
	{
		available_controllers = std::move(current_controllers);
		emit AvailableControllersUpdated();
	}
#endif
}

void ControllerManager::creatingControllerMapping(bool creating_controller_mapping)
{
	this->creating_controller_mapping = creating_controller_mapping;
}

void ControllerManager::HandleEvents()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	SDL_Event event;
	while(SDL_PollEvent(&event))
	{
		switch(event.type)
		{
			case SDL_JOYDEVICEADDED:
			case SDL_JOYDEVICEREMOVED:
				UpdateAvailableControllers();
				break;
			case SDL_JOYAXISMOTION:
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
			case SDL_JOYHATMOTION:
			case SDL_CONTROLLERBUTTONUP:
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERAXISMOTION:
#if not defined(CHIAKI_ENABLE_SETSU) and SDL_VERSION_ATLEAST(2, 0, 14)
			case SDL_CONTROLLERSENSORUPDATE:
			case SDL_CONTROLLERTOUCHPADDOWN:
			case SDL_CONTROLLERTOUCHPADMOTION:
			case SDL_CONTROLLERTOUCHPADUP:
#endif
				if(joystick_allow_background_events || is_app_active)
					ControllerEvent(event);
				break;
		}
	}
#endif
}

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
void ControllerManager::ControllerEvent(SDL_Event event)
{
	int device_id;
	bool start_updating_mapping = false;
	switch(event.type)
	{
		case SDL_CONTROLLERBUTTONDOWN:
			device_id = event.cbutton.which;
			break;
		case SDL_CONTROLLERBUTTONUP:
			device_id = event.cbutton.which;
			break;
		case SDL_CONTROLLERAXISMOTION:
			device_id = event.caxis.which;
			break;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLERSENSORUPDATE:
			device_id = event.csensor.which;
			break;
		case SDL_CONTROLLERTOUCHPADDOWN:
			device_id = event.ctouchpad.which;
			break;
		case SDL_CONTROLLERTOUCHPADMOTION:
		case SDL_CONTROLLERTOUCHPADUP:
			device_id = event.ctouchpad.which;
			break;
#endif
		case SDL_JOYAXISMOTION:
			device_id = event.jaxis.which;
			break;
		case SDL_JOYBUTTONDOWN:
			start_updating_mapping = true;
			device_id = event.jbutton.which;
			break;
		case SDL_JOYBUTTONUP:
			device_id = event.jbutton.which;
			break;
		case SDL_JOYHATMOTION:
			start_updating_mapping = true;
			device_id = event.jhat.which;
			break;
		default:
			return;
	}
	if(!open_controllers.contains(device_id))
		return;
	if(creating_controller_mapping && start_updating_mapping)
	{
		open_controllers[device_id]->StartUpdatingMapping();
		creating_controller_mapping = false;
	}
	open_controllers[device_id]->UpdateState(event);
}
#endif

QSet<int> ControllerManager::GetAvailableControllers()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	return available_controllers;
#else
	return {};
#endif
}

Controller *ControllerManager::OpenController(int device_id)
{
	Controller *controller = open_controllers.value(device_id);
	if(!controller)
	{
		controller = new Controller(device_id, this);
		open_controllers[device_id] = controller;
	}
	controller->Ref();
	return controller;
}

void ControllerManager::ControllerClosed(Controller *controller)
{
	open_controllers.remove(controller->GetDeviceID());
}

Controller::Controller(int device_id, ControllerManager *manager)
: QObject(manager), ref(0), last_motion_timestamp(0), micbutton_push(false), is_dualsense(false),
  is_dualsense_edge(false), has_led(false), firmware_version(0), updating_mapping_button(false), is_handheld(false),
  is_steam_virtual(false), is_steam_virtual_unmasked(false), enable_analog_stick_mapping(false)
{
	this->id = device_id;
	this->manager = manager;
	chiaki_orientation_tracker_init(&this->orientation_tracker);
	chiaki_accel_new_zero_set_inactive(&this->accel_zero, false);
	chiaki_accel_new_zero_set_inactive(&this->real_accel, true);
	chiaki_controller_state_set_idle(&this->state);

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	controller = nullptr;
	for(int i=0; i<SDL_NumJoysticks(); i++)
	{
		if(SDL_JoystickGetDeviceInstanceID(i) == device_id)
		{
			controller = SDL_GameControllerOpen(i);
#if SDL_VERSION_ATLEAST(2, 0, 14)
			if(SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL))
				SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE);
			if(SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO))
				SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE);
#endif
			has_led = SDL_GameControllerHasLED(controller);
			auto controller_id = QPair<uint16_t, uint16_t>(SDL_GameControllerGetVendor(controller), SDL_GameControllerGetProduct(controller));
			is_dualsense = chiaki_dualsense_controller_ids.contains(controller_id);
			is_handheld = chiaki_handheld_controller_ids.contains(controller_id);
			is_dualsense_edge = chiaki_dualsense_edge_controller_ids.contains(controller_id);
			firmware_version = SDL_GameControllerGetFirmwareVersion(controller);
			SDL_Joystick *js = SDL_GameControllerGetJoystick(controller);
			SDL_JoystickGUID guid = SDL_JoystickGetGUID(js);
			auto guid_controller_id = QPair<uint16_t, uint16_t>(0, 0);
			uint16_t guid_version = 0;
			SDL_GetJoystickGUIDInfo(guid, &guid_controller_id.first, &guid_controller_id.second, &guid_version, NULL);
#ifdef Q_OS_MACOS
			is_steam_virtual = (guid_version == 0 && chiaki_steam_virtual_controller_ids.contains(guid_controller_id));
			is_steam_virtual_unmasked = (guid_version == 0 && chiaki_steam_virtual_controller_ids.contains(controller_id));
#else
			is_steam_virtual = chiaki_steam_virtual_controller_ids.contains(guid_controller_id);
			is_steam_virtual_unmasked = chiaki_steam_virtual_controller_ids.contains(controller_id);
#endif
			break;
		}
	}
#endif
}

Controller::~Controller()
{
	Q_ASSERT(ref == 0);
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(controller && SDL_WasInit(SDL_INIT_GAMECONTROLLER)!=0)
	{
		// Clear trigger effects, SDL doesn't do it automatically
		const uint8_t clear_effect[10] = { 0 };
		this->SetTriggerEffects(0x05, clear_effect, 0x05, clear_effect);
		this->SetRumble(0,0);
		SDL_GameControllerClose(controller);
	}
#endif
}

void Controller::StartUpdatingMapping()
{
	emit UpdatingControllerMapping(this);
}

void Controller::IsUpdatingMappingButton(bool is_updating_mapping_button)
{
	this->updating_mapping_button = is_updating_mapping_button;
}

void Controller::EnableAnalogStickMapping(bool enabled)
{
	this->enable_analog_stick_mapping = enabled;
}

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
void Controller::UpdateState(SDL_Event event)
{
	switch(event.type)
	{
		case SDL_JOYAXISMOTION:
			if(updating_mapping_button && enable_analog_stick_mapping)
			{
				emit NewButtonMapping(QString("a%1").arg(QString::number(event.jaxis.axis)));
				updating_mapping_button = false;
			}
			return;
		case SDL_JOYBUTTONDOWN:
			if(updating_mapping_button)
			{
				emit NewButtonMapping(QString("b%1").arg(QString::number(event.jbutton.button)));
				updating_mapping_button = false;
			}
			return;
		case SDL_JOYBUTTONUP:
			return;
		case SDL_JOYHATMOTION:
			if(updating_mapping_button)
			{
				emit NewButtonMapping(QString("h%1.%2").arg(QString::number(event.jhat.hat)).arg(QString::number(event.jhat.value)));
				updating_mapping_button = false;
			}
			return;
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			if(!HandleButtonEvent(event.cbutton))
				return;
			break;
		case SDL_CONTROLLERAXISMOTION:
			if(!HandleAxisEvent(event.caxis))
				return;
			break;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLERSENSORUPDATE:
			if(!HandleSensorEvent(event.csensor))
				return;
			break;
		case SDL_CONTROLLERTOUCHPADDOWN:
		case SDL_CONTROLLERTOUCHPADMOTION:
		case SDL_CONTROLLERTOUCHPADUP:
			if(!HandleTouchpadEvent(event.ctouchpad))
				return;
			break;
#endif
		default:
			return;

	}
	emit StateChanged();
	manager->moved = true;
}

inline bool Controller::HandleButtonEvent(SDL_ControllerButtonEvent event) {
	ChiakiControllerButton ps_btn;
	switch(event.button)
	{
		case SDL_CONTROLLER_BUTTON_A:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_CROSS;
			break;
		case SDL_CONTROLLER_BUTTON_B:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_MOON;
			break;
		case SDL_CONTROLLER_BUTTON_X:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_BOX;
			break;
		case SDL_CONTROLLER_BUTTON_Y:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_PYRAMID;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
			break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_L1;
			break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_R1;
			break;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_L3;
			break;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_R3;
			break;
		case SDL_CONTROLLER_BUTTON_START:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_OPTIONS;
			break;
		case SDL_CONTROLLER_BUTTON_BACK:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_SHARE;
			break;
		case SDL_CONTROLLER_BUTTON_GUIDE:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_PS;
			break;
		case SDL_CONTROLLER_BUTTON_MISC1:
			micbutton_push = true;
			break;
		case SDL_CONTROLLER_BUTTON_PADDLE1:
			return false;
		case SDL_CONTROLLER_BUTTON_PADDLE2:
			return false;
		case SDL_CONTROLLER_BUTTON_PADDLE3:
			return false;
		case SDL_CONTROLLER_BUTTON_PADDLE4:
			return false;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLER_BUTTON_TOUCHPAD:
			ps_btn = CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
			break;
#endif
		default:
			return false;
		}
	if(event.type == SDL_CONTROLLERBUTTONDOWN)
	{
		if(!micbutton_push)
			state.buttons |= ps_btn;
	}
	else
	{
		if(micbutton_push)
		{
			micbutton_push = false;
			emit MicButtonPush();
			return false;
		}
		else
			state.buttons &= ~ps_btn;
	}
	return true;
}

inline bool Controller::HandleAxisEvent(SDL_ControllerAxisEvent event) {
	switch(event.axis)
	{
		case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
			state.l2_state = (uint8_t)(event.value >> 7);
			break;
		case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
			state.r2_state = (uint8_t)(event.value >> 7);
			break;
		case SDL_CONTROLLER_AXIS_LEFTX:
			state.left_x = event.value;
			break;
		case SDL_CONTROLLER_AXIS_LEFTY:
			state.left_y = event.value;
			break;
		case SDL_CONTROLLER_AXIS_RIGHTX:
			state.right_x = event.value;
			break;
		case SDL_CONTROLLER_AXIS_RIGHTY:
			state.right_y = event.value;
			break;
		default:
			return false;
	}
	return true;
}

#if SDL_VERSION_ATLEAST(2, 0, 14)
inline bool Controller::HandleSensorEvent(SDL_ControllerSensorEvent event)
{
	bool ds5_fix = manager->GetDS5GyroFixEnabled() && IsDualSense();
	if(ds5_fix)
		orientation_tracker.fuzz_enabled = false;
	float accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z;
	switch(event.sensor)
	{
		case SDL_SENSOR_ACCEL:
			accel_x = event.data[0] / SDL_STANDARD_GRAVITY;
			accel_y = event.data[1] / SDL_STANDARD_GRAVITY;
			accel_z = event.data[2] / SDL_STANDARD_GRAVITY;
			if(ds5_fix)
			{
				// v1.8.0: write raw accel to state, feed Madgwick with paired latest values, no accel_zero/fuzz
				state.accel_x = accel_x;
				state.accel_y = accel_y;
				state.accel_z = accel_z;
				chiaki_orientation_tracker_update(
					&orientation_tracker, state.gyro_x, state.gyro_y, state.gyro_z,
					state.accel_x, state.accel_y, state.accel_z, &accel_zero, true, event.timestamp * 1000);
			}
			else
			{
				chiaki_accel_new_zero_set_active(&this->real_accel,
				accel_x, accel_y, accel_z, true);
				chiaki_orientation_tracker_update(
					&orientation_tracker, state.gyro_x, state.gyro_y, state.gyro_z,
					accel_x, accel_y, accel_z, &accel_zero, false, event.timestamp * 1000);
			}
			break;
		case SDL_SENSOR_GYRO:
			gyro_x = event.data[0];
			gyro_y = event.data[1];
			gyro_z = event.data[2];
			if(ds5_fix)
			{
				// v1.8.0: write raw gyro to state, feed Madgwick with paired latest values, no accel_zero/fuzz
				state.gyro_x = gyro_x;
				state.gyro_y = gyro_y;
				state.gyro_z = gyro_z;
				chiaki_orientation_tracker_update(
					&orientation_tracker, state.gyro_x, state.gyro_y, state.gyro_z,
					state.accel_x, state.accel_y, state.accel_z, &accel_zero, true, event.timestamp * 1000);
			}
			else
			{
				chiaki_orientation_tracker_update(
					&orientation_tracker, gyro_x, gyro_y, gyro_z,
					state.accel_x, state.accel_y, state.accel_z, &accel_zero, true, event.timestamp * 1000);
			}
			break;
		default:
			return false;
	}
	last_motion_timestamp = event.timestamp * 1000;
	chiaki_orientation_tracker_apply_to_controller_state(&orientation_tracker, &state);
	return true;
}

inline bool Controller::HandleTouchpadEvent(SDL_ControllerTouchpadEvent event)
{
	auto key = qMakePair(event.touchpad, event.finger);
	bool exists = touch_ids.contains(key);
	uint8_t chiaki_id;
	switch(event.type)
	{
		case SDL_CONTROLLERTOUCHPADDOWN:
			if(touch_ids.size() >= CHIAKI_CONTROLLER_TOUCHES_MAX)
				return false;
			chiaki_id = chiaki_controller_state_start_touch(&state, event.x * PS_TOUCHPAD_MAXX, event.y * PS_TOUCHPAD_MAXY);
			touch_ids.insert(key, chiaki_id);
			break;
		case SDL_CONTROLLERTOUCHPADMOTION:
			if(!exists)
				return false;
			chiaki_controller_state_set_touch_pos(&state, touch_ids[key], event.x * PS_TOUCHPAD_MAXX, event.y * PS_TOUCHPAD_MAXY);
			break;
		case SDL_CONTROLLERTOUCHPADUP:
			if(!exists)
				return false;
			chiaki_controller_state_stop_touch(&state, touch_ids[key]);
			touch_ids.remove(key);
			break;
		default:
			return false;
	}
	return true;
}
#endif
#endif

void Controller::Ref()
{
	ref++;
}

void Controller::Unref()
{
	if(--ref == 0)
	{
		manager->ControllerClosed(this);
		deleteLater();
	}
}

bool Controller::IsConnected()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	return controller && SDL_GameControllerGetAttached(controller);
#else
	return false;
#endif
}

int Controller::GetDeviceID()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	return id;
#else
	return -1;
#endif
}

QString Controller::GetType()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return QString();
	SDL_Joystick *js = SDL_GameControllerGetJoystick(controller);
	return QString("%1").arg(SDL_JoystickName(js));
#else
	return QString();
#endif
}

bool Controller::IsPS()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	SDL_GameControllerType type = SDL_GameControllerGetType(controller);
	if(type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4 || type == SDL_CONTROLLER_TYPE_PS5)
		return true;
	else
		return false;
#else
	return false;
#endif
}

QString Controller::GetGUIDString()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return QString();
	SDL_Joystick *js = SDL_GameControllerGetJoystick(controller);
	char guid_str[256];
	SDL_JoystickGUID guid = SDL_JoystickGetGUID(js);
	SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
	return QString("%1").arg(guid_str);
#else
	return QString();
#endif
}

QString Controller::GetName()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return QString();
	SDL_Joystick *js = SDL_GameControllerGetJoystick(controller);
	SDL_JoystickGUID guid = SDL_JoystickGetGUID(js);
	char guid_str[256];
	SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
	return QString("%1 (%2)").arg(SDL_JoystickName(js), guid_str);
#else
	return QString();
#endif
}

QString Controller::GetVIDPIDString()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return QString();
	QString vid_pid = QString("0x%1:0x%2").arg(SDL_GameControllerGetVendor(controller), 4, 16, QChar('0')).arg(SDL_GameControllerGetProduct(controller), 4, 16, QChar('0'));
	return vid_pid;
#else
	return QString();
#endif
}

ChiakiControllerState Controller::GetState()
{
	return state;
}

bool Controller::IsWired()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
	if(!joystick)
		return false;
	return CHIAKI_SDL_JOYSTICK_GET_POWERLEVEL(joystick) == SDL_JOYSTICK_POWER_WIRED;
#else
	return false;
#endif
}
void Controller::SetDualSenseRumble(uint8_t left, uint8_t right)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return;
	if(!is_dualsense && !is_dualsense_edge)
		return;
	DS5EffectsState_t state;
	SDL_zero(state);
	// SDL_GameControllerGetFirmwareVersion() can return 0 on hotplug or with
	// some USB/BT driver combinations. 0 is not a valid firmware version, so
	// treat it as "new firmware" (>= 2.24). Otherwise we would send the legacy
	// 0x01 enable bit, which current DualSense firmware ignores, causing rumble
	// to silently disappear (e.g. when switching from Bluetooth to USB).
	if(firmware_version != 0 && firmware_version < 0x0224)
	{
		state.ucEnableBits1 |= 0x01;
		state.ucRumbleLeft = left >> 1;
		state.ucRumbleRight = right >> 1;
	}
	else
	{
		state.ucEnableBits3 |= 0x04;
		state.ucRumbleLeft = left;
		state.ucRumbleRight = right;
	}
	state.rgucUnknown1[4] = manager->GetDualSenseIntensity();
	state.ucEnableBits2 |= 0x40;
	state.ucEnableBits1 |= 0x02;
	SDL_GameControllerSendEffect(controller, &state, sizeof(state));
#endif
}

void Controller::SetRumble(uint8_t left, uint8_t right)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return;
	if(is_dualsense || is_dualsense_edge)
		SetDualSenseRumble(left, right);
	else
		SDL_GameControllerRumble(controller, (uint16_t)left << 8, (uint16_t)right << 8, 5000);
#endif
}

void Controller::ChangeLEDColor(const uint8_t *led_color)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller || !has_led)
		return;
	SDL_GameControllerSetLED(controller, led_color[0], led_color[1], led_color[2]);
#endif
}

void Controller::ChangePlayerIndex(const uint8_t player_index)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return;
	SDL_GameControllerSetPlayerIndex(controller, player_index);
#endif
}

void Controller::SetTriggerEffects(uint8_t type_left, const uint8_t *data_left, uint8_t type_right, const uint8_t *data_right)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if((!is_dualsense && !is_dualsense_edge) || !controller)
		return;
	DS5EffectsState_t state;
	SDL_zero(state);
	state.rgucUnknown1[4] = manager->GetDualSenseIntensity();
	state.ucEnableBits2 |= 0x40;
	state.ucEnableBits1 |= (0x04 /* left trigger */ | 0x08 /* right trigger */);
	state.rgucLeftTriggerEffect[0] = type_left;
	SDL_memcpy(state.rgucLeftTriggerEffect + 1, data_left, 10);
	state.rgucRightTriggerEffect[0] = type_right;
	SDL_memcpy(state.rgucRightTriggerEffect + 1, data_right, 10);
	SDL_GameControllerSendEffect(controller, &state, sizeof(state));
#endif
}

void Controller::SetDualsenseMic(bool on)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if((!is_dualsense && !is_dualsense_edge) || !controller)
		return;
	DS5EffectsState_t state;
	SDL_zero(state);
	state.ucEnableBits2 |= 0x01 /* mic light */ | 0x02 /* mic */;
	if(on)
	{
		state.ucMicLightMode = 0x01;
		state.ucAudioMuteBits = 0x08;
	}
	else
	{
		state.ucMicLightMode = 0x00;
		state.ucAudioMuteBits = 0x00;
	}
	SDL_GameControllerSendEffect(controller, &state, sizeof(state));
#endif
}

void Controller::SetHapticRumble(uint16_t left, uint16_t right)
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return;
	if(is_dualsense || is_dualsense_edge)
		SetDualSenseRumble(left >> 8, right >> 8);
	else
		SDL_GameControllerRumble(controller, left, right, 5000);
#endif
}

bool Controller::IsDualSense()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	return is_dualsense;
#endif
	return false;
}

bool Controller::IsDualSenseEdge()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	return is_dualsense_edge;
#endif
	return false;
}

bool Controller::IsHandheld()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	return is_handheld;
#endif
	return false;
}

bool Controller::IsSteamVirtual()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	return is_steam_virtual;
#endif
	return false;
}

bool Controller::IsSteamVirtualUnmasked()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return false;
	return is_steam_virtual_unmasked;
#endif
	return false;
}

void Controller::resetMotionControls()
{
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	if(!controller)
		return;
	if(manager->GetDS5GyroFixEnabled() && IsDualSense())
		return; // v1.8.0 did not handle motion reset for SDL controllers
	chiaki_accel_new_zero_set_active(&accel_zero, real_accel.accel_x, real_accel.accel_y, real_accel.accel_z, false);
	chiaki_orientation_tracker_init(&orientation_tracker);
	chiaki_orientation_tracker_update(
		&orientation_tracker, state.gyro_x, state.gyro_y, state.gyro_z,
		real_accel.accel_x, real_accel.accel_y, real_accel.accel_z, &accel_zero, false, last_motion_timestamp);
	chiaki_orientation_tracker_apply_to_controller_state(&orientation_tracker, &state);
#endif
}
