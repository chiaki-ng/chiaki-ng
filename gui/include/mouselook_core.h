// SPDX-License-Identifier: AGPL-3.0-only
//
// Mouse look for chiaki-ng (PadBridge patch).
//
// Turns "which keys are down + how far the mouse moved" into controller state, so a mouse and
// keyboard can play a PS5 game over Remote Play with NO virtual-gamepad driver.
// This file is plain C++17 with no Qt, so the logic can be tested on its own.
//
// Config file: mouselook.ini next to the program (see mouselook.ini.example in the PadBridge folder).

#ifndef CHIAKI_MOUSELOOK_CORE_H
#define CHIAKI_MOUSELOOK_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mouselook
{

// Same values as ChiakiControllerButton (checked with static_assert in streamsession.cpp).
enum : uint32_t
{
	BTN_CROSS = 1u << 0, BTN_CIRCLE = 1u << 1, BTN_SQUARE = 1u << 2, BTN_TRIANGLE = 1u << 3,
	BTN_DPAD_LEFT = 1u << 4, BTN_DPAD_RIGHT = 1u << 5, BTN_DPAD_UP = 1u << 6, BTN_DPAD_DOWN = 1u << 7,
	BTN_L1 = 1u << 8, BTN_R1 = 1u << 9, BTN_L3 = 1u << 10, BTN_R3 = 1u << 11,
	BTN_OPTIONS = 1u << 12, BTN_SHARE = 1u << 13, BTN_TOUCHPAD = 1u << 14, BTN_PS = 1u << 15
};

enum Mode { MODE_HOLD, MODE_TOGGLE, MODE_TURBO, MODE_TAP };
enum Analog { ANALOG_NONE, ANALOG_L2, ANALOG_R2 };
enum Stick { STICK_NONE, LS_UP, LS_DOWN, LS_LEFT, LS_RIGHT, RS_UP, RS_DOWN, RS_LEFT, RS_RIGHT };

struct Binding
{
	uint32_t buttons = 0;
	int analog = ANALOG_NONE;
	int stick = STICK_NONE;
	Mode mode = MODE_HOLD;
};

struct ProfileDef
{
	std::string name;
	bool mouse_stick = true;
	std::vector<std::pair<std::string, std::string>> keys;     // key name -> "TARGET[,mode]"
};

struct Config
{
	bool enabled = false;
	float sensitivity = 1.0f;
	float ads_multiplier = 0.6f;
	float curve = 1.2f;
	float deadzone_offset = 0.12f;
	float smoothing = 0.6f;
	bool invert_y = false;
	std::string toggle_key = "F8";
	std::string profile_key = "F7";
	std::string walk_key = "Alt";
	float walk_scale = 0.5f;
	std::vector<ProfileDef> profiles;
};

// ------------------------------------------------------------------ small helpers
inline std::string Lower(std::string s)
{
	for(auto &c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

inline std::string Upper(std::string s)
{
	for(auto &c : s)
		c = (char)std::toupper((unsigned char)c);
	return s;
}

inline std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while(a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
		a++;
	while(b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
		b--;
	return s.substr(a, b - a);
}

inline double Clamp(double v, double lo, double hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

inline bool ParseBool(const std::string &v)
{
	std::string l = Lower(Trim(v));
	return l == "1" || l == "true" || l == "yes" || l == "on";
}

inline float ParseFloat(const std::string &v, float fallback)
{
	std::string t = Trim(v);
	if(t.empty())
		return fallback;
	char *end = nullptr;
	float f = std::strtof(t.c_str(), &end);
	return (end && end != t.c_str()) ? f : fallback;
}

// ------------------------------------------------------------------ ini file
inline Config ParseIni(const std::string &text)
{
	Config cfg;
	std::string section;
	std::string original_section;
	size_t pos = 0;
	while(pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		std::string line = Trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
		if(line.empty() || line[0] == ';' || line[0] == '#')
			continue;
		if(line[0] == '[')
		{
			size_t close = line.find(']');
			original_section = Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
			section = Lower(original_section);
			if(section.rfind("profile:", 0) == 0)
			{
				ProfileDef p;
				p.name = Trim(original_section.substr(8));
				cfg.profiles.push_back(p);
			}
			continue;
		}
		size_t eq = line.find('=');
		if(eq == std::string::npos)
			continue;
		std::string key = Trim(line.substr(0, eq));
		std::string val = Trim(line.substr(eq + 1));
		std::string lkey = Lower(key);
		if(section == "mouse")
		{
			if(lkey == "enabled") cfg.enabled = ParseBool(val);
			else if(lkey == "sensitivity") cfg.sensitivity = ParseFloat(val, cfg.sensitivity);
			else if(lkey == "ads_multiplier") cfg.ads_multiplier = ParseFloat(val, cfg.ads_multiplier);
			else if(lkey == "curve") cfg.curve = ParseFloat(val, cfg.curve);
			else if(lkey == "deadzone_offset") cfg.deadzone_offset = ParseFloat(val, cfg.deadzone_offset);
			else if(lkey == "smoothing") cfg.smoothing = ParseFloat(val, cfg.smoothing);
			else if(lkey == "invert_y") cfg.invert_y = ParseBool(val);
			else if(lkey == "toggle_key") cfg.toggle_key = val;
			else if(lkey == "profile_key") cfg.profile_key = val;
		}
		else if(section == "walk")
		{
			if(lkey == "key") cfg.walk_key = val;
			else if(lkey == "scale") cfg.walk_scale = ParseFloat(val, cfg.walk_scale);
		}
		else if(section.rfind("profile:", 0) == 0 && !cfg.profiles.empty())
		{
			if(lkey == "mouse_stick")
			{
				std::string l = Lower(val);
				cfg.profiles.back().mouse_stick = (l == "rs" || l == "right" || ParseBool(val));
			}
			else
				cfg.profiles.back().keys.emplace_back(key, val);
		}
	}
	return cfg;
}

// ------------------------------------------------------------------ targets
inline bool ParseBinding(const std::string &text, Binding &out)
{
	std::string target = text, mode = "hold";
	size_t comma = text.find(',');
	if(comma != std::string::npos)
	{
		target = text.substr(0, comma);
		mode = text.substr(comma + 1);
	}
	target = Upper(Trim(target));
	mode = Lower(Trim(mode));
	Binding b;
	if(mode == "hold") b.mode = MODE_HOLD;
	else if(mode == "toggle") b.mode = MODE_TOGGLE;
	else if(mode == "turbo") b.mode = MODE_TURBO;
	else if(mode == "tap") b.mode = MODE_TAP;
	else return false;

	if(target == "CROSS") b.buttons = BTN_CROSS;
	else if(target == "CIRCLE") b.buttons = BTN_CIRCLE;
	else if(target == "SQUARE") b.buttons = BTN_SQUARE;
	else if(target == "TRIANGLE") b.buttons = BTN_TRIANGLE;
	else if(target == "L1") b.buttons = BTN_L1;
	else if(target == "R1") b.buttons = BTN_R1;
	else if(target == "L3") b.buttons = BTN_L3;
	else if(target == "R3") b.buttons = BTN_R3;
	else if(target == "OPTIONS") b.buttons = BTN_OPTIONS;
	else if(target == "SHARE") b.buttons = BTN_SHARE;
	else if(target == "TOUCHPAD") b.buttons = BTN_TOUCHPAD;
	else if(target == "PS") b.buttons = BTN_PS;
	else if(target == "UP") b.buttons = BTN_DPAD_UP;
	else if(target == "DOWN") b.buttons = BTN_DPAD_DOWN;
	else if(target == "LEFT") b.buttons = BTN_DPAD_LEFT;
	else if(target == "RIGHT") b.buttons = BTN_DPAD_RIGHT;
	else if(target == "L2") b.analog = ANALOG_L2;
	else if(target == "R2") b.analog = ANALOG_R2;
	else if(target == "LS_UP") b.stick = LS_UP;
	else if(target == "LS_DOWN") b.stick = LS_DOWN;
	else if(target == "LS_LEFT") b.stick = LS_LEFT;
	else if(target == "LS_RIGHT") b.stick = LS_RIGHT;
	else if(target == "RS_UP") b.stick = RS_UP;
	else if(target == "RS_DOWN") b.stick = RS_DOWN;
	else if(target == "RS_LEFT") b.stick = RS_LEFT;
	else if(target == "RS_RIGHT") b.stick = RS_RIGHT;
	else return false;
	out = b;
	return true;
}

// ------------------------------------------------------------------ the mapper
using Resolver = std::function<int(const std::string &)>;     // key name -> key id, or -1

class Mapper
{
public:
	struct Output
	{
		uint32_t buttons = 0;
		uint8_t l2 = 0, r2 = 0;
		int16_t left_x = 0, left_y = 0;      // chiaki convention: +y = DOWN
		int16_t right_x = 0, right_y = 0;
	};

	static constexpr double REF_SPEED = 1800.0;     // mouse px/s that gives a full stick at sensitivity 1.0

	Mapper(const Config &cfg, Resolver res) { Configure(cfg, std::move(res)); }

	void Configure(const Config &cfg, Resolver res)
	{
		cfg_ = cfg;
		profiles_.clear();
		for(const auto &pd : cfg.profiles)
		{
			Profile p;
			p.name = pd.name;
			p.mouse_stick = pd.mouse_stick;
			for(const auto &kv : pd.keys)
			{
				int id = res(kv.first);
				Binding b;
				if(id >= 0 && ParseBinding(kv.second, b))
					p.bindings[id] = b;
			}
			profiles_.push_back(p);
		}
		if(cur_ >= profiles_.size())
			cur_ = 0;
		toggle_key_ = res(cfg.toggle_key);
		profile_key_ = res(cfg.profile_key);
		walk_key_ = cfg.walk_key.empty() ? -1 : res(cfg.walk_key);
		ReleaseAll();
	}

	bool Enabled() const { return cfg_.enabled && !profiles_.empty(); }
	int ToggleKey() const { return toggle_key_; }
	int ProfileKey() const { return profile_key_; }
	std::string ProfileName() const { return profiles_.empty() ? std::string() : profiles_[cur_].name; }

	bool IsMapped(int key) const
	{
		if(profiles_.empty())
			return false;
		return profiles_[cur_].bindings.count(key) > 0 || key == walk_key_;
	}

	void SetKey(int key, bool down)
	{
		if(down) down_[key] = true;
		else down_.erase(key);
	}

	void Pulse(int key, double now) { pulse_until_[key] = now + 0.06; }
	void AddMouse(double dx, double dy) { pend_dx_ += dx; pend_dy_ += dy; }

	void ReleaseAll()
	{
		down_.clear();
		pulse_until_.clear();
		prev_.clear();
		tap_until_.clear();
		pend_dx_ = pend_dy_ = 0.0;
		sm_[0] = sm_[1] = 0.0;
		last_t_ = -1.0;
	}

	void NextProfile()
	{
		if(profiles_.empty())
			return;
		cur_ = (cur_ + 1) % profiles_.size();
		ReleaseAll();
	}

	Output Update(double now)
	{
		Output out;
		if(profiles_.empty())
			return out;
		const double dt = last_t_ < 0.0 ? 0.004 : Clamp(now - last_t_, 0.001, 0.05);
		last_t_ = now;
		const Profile &p = profiles_[cur_];

		uint32_t btn = 0;
		bool l2 = false, r2 = false;
		double lx = 0, ly = 0, rx = 0, ry = 0;      // +y = up while we work
		for(const auto &kv : p.bindings)
		{
			const int key = kv.first;
			const Binding &b = kv.second;
			const bool is_down = IsDown(key, now);
			const bool was = prev_[key];
			const bool pressed_now = is_down && !was;
			prev_[key] = is_down;
			bool active = false;
			switch(b.mode)
			{
				case MODE_HOLD:
					active = is_down;
					break;
				case MODE_TOGGLE:
					if(pressed_now)
						toggles_[key] = !toggles_[key];
					active = toggles_[key];
					break;
				case MODE_TURBO:
					active = is_down && (static_cast<long long>(now / 0.06) % 2 == 0);
					break;
				case MODE_TAP:
					if(pressed_now)
						tap_until_[key] = now + 0.08;
					active = now < tap_until_[key];
					break;
			}
			if(!active)
				continue;
			btn |= b.buttons;
			if(b.analog == ANALOG_L2) l2 = true;
			if(b.analog == ANALOG_R2) r2 = true;
			switch(b.stick)
			{
				case LS_UP: ly += 1; break;
				case LS_DOWN: ly -= 1; break;
				case LS_LEFT: lx -= 1; break;
				case LS_RIGHT: lx += 1; break;
				case RS_UP: ry += 1; break;
				case RS_DOWN: ry -= 1; break;
				case RS_LEFT: rx -= 1; break;
				case RS_RIGHT: rx += 1; break;
				default: break;
			}
		}

		// diagonals no faster than straight lines
		const double m = std::hypot(lx, ly);
		if(m > 1.0) { lx /= m; ly /= m; }
		if(walk_key_ >= 0 && IsDown(walk_key_, now))
		{
			const double sc = Clamp(cfg_.walk_scale, 0.05, 1.0);
			lx *= sc; ly *= sc;
		}

		// mouse -> right stick
		if(p.mouse_stick)
		{
			double sens = cfg_.sensitivity / REF_SPEED;
			if(l2) sens *= cfg_.ads_multiplier;
			const double vx = pend_dx_ / dt * sens;
			const double vy = (cfg_.invert_y ? pend_dy_ : -pend_dy_) / dt * sens;     // screen y grows downward
			const double a = Clamp(cfg_.smoothing, 0.05, 1.0);
			sm_[0] += (Shape(vx) - sm_[0]) * a;
			sm_[1] += (Shape(vy) - sm_[1]) * a;
			for(int i = 0; i < 2; i++)
				if(std::fabs(sm_[i]) < 0.01) sm_[i] = 0.0;
			rx = Clamp(rx + sm_[0], -1.0, 1.0);
			ry = Clamp(ry + sm_[1], -1.0, 1.0);
		}
		else
			sm_[0] = sm_[1] = 0.0;
		pend_dx_ = pend_dy_ = 0.0;

		out.buttons = btn;
		out.l2 = l2 ? 0xff : 0;
		out.r2 = r2 ? 0xff : 0;
		out.left_x = ToAxis(lx);
		out.left_y = ToAxis(-ly);
		out.right_x = ToAxis(rx);
		out.right_y = ToAxis(-ry);
		return out;
	}

	// raw mouse speed (-1..1) -> stick value with response curve and deadzone offset
	double Shape(double v) const
	{
		if(v == 0.0)
			return 0.0;
		const double sign = v > 0 ? 1.0 : -1.0;
		const double a = std::pow(Clamp(std::fabs(v), 0.0, 1.0), std::max(0.1, (double)cfg_.curve));
		const double off = Clamp(cfg_.deadzone_offset, 0.0, 0.9);
		return sign * (off + a * (1.0 - off));
	}

private:
	struct Profile
	{
		std::string name;
		bool mouse_stick = true;
		std::map<int, Binding> bindings;
	};

	static int16_t ToAxis(double v) { return static_cast<int16_t>(std::lround(Clamp(v, -1.0, 1.0) * 32767.0)); }

	bool IsDown(int key, double now) const
	{
		if(down_.count(key))
			return true;
		auto it = pulse_until_.find(key);
		return it != pulse_until_.end() && now < it->second;
	}

	Config cfg_;
	std::vector<Profile> profiles_;
	size_t cur_ = 0;
	int toggle_key_ = -1, profile_key_ = -1, walk_key_ = -1;
	std::map<int, bool> down_;
	std::map<int, double> pulse_until_;
	std::map<int, bool> prev_;
	std::map<int, bool> toggles_;
	std::map<int, double> tap_until_;
	double pend_dx_ = 0.0, pend_dy_ = 0.0;
	double sm_[2] = {0.0, 0.0};
	double last_t_ = -1.0;
};

} // namespace mouselook

#endif // CHIAKI_MOUSELOOK_CORE_H
