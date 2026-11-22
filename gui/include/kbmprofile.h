// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Keyboard + mouse ("KBM") profile and mouse-to-stick aim mapping.
// Pure C++ (no Qt) so that the logic can be unit tested on its own.

#ifndef CHIAKI_KBMPROFILE_H
#define CHIAKI_KBMPROFILE_H

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace kbm
{

enum class Action
{
	Cross, Circle, Square, Triangle,
	L1, R1, L2, R2, L3, R3,
	Options, Create, Touchpad, PS,
	DpadUp, DpadDown, DpadLeft, DpadRight,
	LsUp, LsDown, LsLeft, LsRight,
	RsUp, RsDown, RsLeft, RsRight,
	Walk,
	Count
};

constexpr size_t ActionCount = static_cast<size_t>(Action::Count);

const char *ActionName(Action action);
std::optional<Action> ParseActionName(const std::string &name);

// Minimal INI document that keeps comments and layout when a value is changed.
class IniDocument
{
	public:
		static IniDocument Parse(const std::string &text);
		std::optional<std::string> Get(const std::string &section, const std::string &key) const;
		// All key/value pairs of a section, in file order.
		std::vector<std::pair<std::string, std::string>> Entries(const std::string &section) const;
		void Set(const std::string &section, const std::string &key, const std::string &value);
		std::string ToString() const;

	private:
		struct Line
		{
			std::string raw;
			std::string section; // section this line belongs to (lower-case)
			std::string key;     // lower-case, empty if not a key/value line
			std::string value;
		};
		std::vector<Line> lines;
};

struct AimConfig
{
	// Stick deflection (0..1) per mouse count per millisecond.
	double sensitivity = 0.12;
	// Response curve applied to the scaled mouse speed. 1.0 = linear, >1 = slower for small moves.
	double exponent = 1.0;
	// Minimum stick deflection as soon as the mouse moves, to jump over the game's inner deadzone.
	double anti_deadzone = 0.18;
	// Maximum stick deflection (0..1).
	double max_output = 1.0;
	// Time window (ms) over which mouse speed is averaged. Larger = smoother but more "glide".
	double smoothing_ms = 12.0;
	// Multiplier for vertical movement.
	double y_scale = 1.0;
	bool invert_y = false;
	// Sensitivity multiplier while L2 (aim) is held.
	double ads_scale = 1.0;
	// Left stick deflection while the "walk" modifier is held.
	double walk_scale = 0.45;
	// How often the stick is recomputed while capturing (ms).
	double update_interval_ms = 4.0;
};

struct Binding
{
	std::string input; // key or mouse input name as written in the profile, e.g. "W", "MouseLeft"
	Action action;
};

struct Profile
{
	AimConfig aim;
	bool click_to_capture = true;
	std::vector<Binding> bindings;
	std::vector<std::string> warnings;
};

Profile ParseProfile(const std::string &text);
const char *DefaultProfileText();

// Converts relative mouse motion into a right stick position.
class AimMapper
{
	public:
		void Configure(const AimConfig &config) { this->config = config; }
		const AimConfig &Config() const { return config; }
		void AddDelta(double dx, double dy) { acc_dx += dx; acc_dy += dy; }
		void Reset();
		// now_ms must be monotonic. Returns stick values in [-32767, 32767], +y = down.
		void Update(double now_ms, bool ads, int16_t *out_x, int16_t *out_y);

	private:
		struct Sample { double t, dx, dy; };
		AimConfig config;
		std::deque<Sample> samples;
		double acc_dx = 0.0, acc_dy = 0.0;
};

// Controller output computed from held actions. Field semantics match ChiakiControllerState.
struct PadOutput
{
	uint32_t buttons = 0;
	uint8_t l2 = 0, r2 = 0;
	int16_t left_x = 0, left_y = 0, right_x = 0, right_y = 0;
};

PadOutput ComputePad(const std::array<bool, ActionCount> &held, double walk_scale);

} // namespace kbm

#endif // CHIAKI_KBMPROFILE_H
