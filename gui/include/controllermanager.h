// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_CONTROLLERMANAGER_H
#define CHIAKI_CONTROLLERMANAGER_H

#include <chiaki/controller.h>

#include <QObject>
#include <QSet>
#include <QMap>
#include <QString>
#include <QTimer>

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
#include <SDL.h>
#include <chiaki/orientation.h>
#endif

#define PS_TOUCHPAD_MAXX 1920
#define PS_TOUCHPAD_MAXY 1079

class Controller;

class ControllerManager : public QObject
{
	Q_OBJECT

	friend class Controller;

	private:
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		QSet<SDL_JoystickID> available_controllers;
#endif
		QMap<int, Controller *> open_controllers;
		bool creating_controller_mapping;
		bool joystick_allow_background_events;
		bool is_app_active;
		bool moved;
		uint8_t dualsense_intensity;
		bool ds5_gyro_fix_enabled;

		void ControllerClosed(Controller *controller);
		void CheckMoved();

	private slots:
		void UpdateAvailableControllers();
		void HandleEvents();
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		void ControllerEvent(SDL_Event evt);
#endif

	public:
		static ControllerManager *GetInstance();

		ControllerManager(QObject *parent = nullptr);
		~ControllerManager();
		void SetButtonsByPos();
		void SetAllowJoystickBackgroundEvents(bool enabled);
		void SetIsAppActive(bool active);
		void SetDualSenseIntensity(uint8_t intensity) { dualsense_intensity = intensity; };
		uint8_t GetDualSenseIntensity() { return dualsense_intensity; };
		void SetDS5GyroFixEnabled(bool enabled) { ds5_gyro_fix_enabled = enabled; };
		bool GetDS5GyroFixEnabled() { return ds5_gyro_fix_enabled; };
		void creatingControllerMapping(bool creating_controller_mapping);
		QSet<int> GetAvailableControllers();
		Controller *OpenController(int device_id);

	signals:
		void AvailableControllersUpdated();
		void ControllerMoved();
};

class Controller : public QObject
{
	Q_OBJECT

	friend class ControllerManager;

	private:
		Controller(int device_id, ControllerManager *manager);

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		void UpdateState(SDL_Event event);
		void SetDualSenseRumble(uint8_t left, uint8_t right);
		bool HandleButtonEvent(SDL_ControllerButtonEvent event);
		bool HandleAxisEvent(SDL_ControllerAxisEvent event);
#if SDL_VERSION_ATLEAST(2, 0, 14)
		bool HandleSensorEvent(SDL_ControllerSensorEvent event);
		bool HandleTouchpadEvent(SDL_ControllerTouchpadEvent event);
#endif
#endif

		int ref;
		ControllerManager *manager;
		int id;
		ChiakiOrientationTracker orientation_tracker;
		ChiakiControllerState state;
		bool updating_mapping_button;
		bool enable_analog_stick_mapping;
		bool is_dualsense;
		bool is_handheld;
		bool is_steam_virtual;
		bool is_steam_virtual_unmasked;
		bool is_dualsense_edge;
		bool has_led;
		bool micbutton_push;
		uint16_t firmware_version;

#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		QMap<QPair<Sint64, Sint64>, uint8_t> touch_ids;
		SDL_GameController *controller;
		ChiakiAccelNewZero accel_zero;
		ChiakiAccelNewZero real_accel;
		uint32_t last_motion_timestamp;
#endif

	public:
		~Controller();

		void Ref();
		void Unref();

		bool IsConnected();
		int GetDeviceID();
#ifdef CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
		SDL_GameController *GetController() { return controller; };
#endif
		QString GetName();
		QString GetVIDPIDString();
		QString GetType();
		bool IsPS();
		QString GetGUIDString();
		ChiakiControllerState GetState();
		// True when the controller reports a wired (USB) power source.
		bool IsWired();
		void SetRumble(uint8_t left, uint8_t right);
		void SetTriggerEffects(uint8_t type_left, const uint8_t *data_left, uint8_t type_right, const uint8_t *data_right);
		void SetDualsenseMic(bool on);
		void SetHapticRumble(uint16_t left, uint16_t right);
		void StartUpdatingMapping();
		void IsUpdatingMappingButton(bool is_updating_mapping_button);
		void EnableAnalogStickMapping(bool enabled);
		void ChangeLEDColor(const uint8_t *led_color);
		void ChangePlayerIndex(const uint8_t player_index);
		bool IsDualSense();
		bool IsHandheld();
		bool IsSteamVirtual();
		bool IsSteamVirtualUnmasked();
		bool IsDualSenseEdge();
		void resetMotionControls();

	signals:
		void StateChanged();
		void MicButtonPush();
		void NewButtonMapping(QString button);
		void UpdatingControllerMapping(Controller* controller);
};
#endif // CHIAKI_CONTROLLERMANAGER_H
