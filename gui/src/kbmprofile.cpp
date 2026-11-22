// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <kbmprofile.h>

#include <chiaki/controller.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace kbm
{

namespace
{

struct ActionInfo
{
	Action action;
	const char *name;
};

const ActionInfo action_infos[] = {
	{ Action::Cross, "cross" },
	{ Action::Circle, "circle" },
	{ Action::Square, "square" },
	{ Action::Triangle, "triangle" },
	{ Action::L1, "l1" },
	{ Action::R1, "r1" },
	{ Action::L2, "l2" },
	{ Action::R2, "r2" },
	{ Action::L3, "l3" },
	{ Action::R3, "r3" },
	{ Action::Options, "options" },
	{ Action::Create, "create" },
	{ Action::Touchpad, "touchpad" },
	{ Action::PS, "ps" },
	{ Action::DpadUp, "dpad_up" },
	{ Action::DpadDown, "dpad_down" },
	{ Action::DpadLeft, "dpad_left" },
	{ Action::DpadRight, "dpad_right" },
	{ Action::LsUp, "ls_up" },
	{ Action::LsDown, "ls_down" },
	{ Action::LsLeft, "ls_left" },
	{ Action::LsRight, "ls_right" },
	{ Action::RsUp, "rs_up" },
	{ Action::RsDown, "rs_down" },
	{ Action::RsLeft, "rs_left" },
	{ Action::RsRight, "rs_right" },
	{ Action::Walk, "walk" },
};

static_assert(sizeof(action_infos) / sizeof(action_infos[0]) == ActionCount, "every action needs a name");

std::string Trim(const std::string &s)
{
	size_t b = 0, e = s.size();
	while(b < e && std::isspace(static_cast<unsigned char>(s[b])))
		b++;
	while(e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		e--;
	return s.substr(b, e - b);
}

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::vector<std::string> SplitList(const std::string &s)
{
	std::vector<std::string> result;
	std::string item;
	std::istringstream stream(s);
	while(std::getline(stream, item, ','))
	{
		item = Trim(item);
		if(!item.empty())
			result.push_back(item);
	}
	return result;
}

// Strips an inline comment (" ;..." or " #...") from a value.
std::string StripInlineComment(const std::string &value)
{
	for(size_t i = 0; i < value.size(); i++)
	{
		if((value[i] == ';' || value[i] == '#') && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1]))))
			return Trim(value.substr(0, i));
	}
	return Trim(value);
}

bool ParseDouble(const std::string &s, double *out)
{
	try
	{
		size_t pos = 0;
		double v = std::stod(s, &pos);
		if(pos != s.size() || !std::isfinite(v))
			return false;
		*out = v;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

bool ParseBool(const std::string &s, bool *out)
{
	std::string l = Lower(s);
	if(l == "true" || l == "1" || l == "yes" || l == "on")
		*out = true;
	else if(l == "false" || l == "0" || l == "no" || l == "off")
		*out = false;
	else
		return false;
	return true;
}

int16_t ToAxis(double v)
{
	v = std::clamp(v, -1.0, 1.0);
	return static_cast<int16_t>(std::lround(v * 32767.0));
}

} // namespace

const char *ActionName(Action action)
{
	for(const auto &info : action_infos)
		if(info.action == action)
			return info.name;
	return "";
}

std::optional<Action> ParseActionName(const std::string &name)
{
	std::string l = Lower(Trim(name));
	for(const auto &info : action_infos)
		if(l == info.name)
			return info.action;
	return std::nullopt;
}

IniDocument IniDocument::Parse(const std::string &text)
{
	IniDocument doc;
	std::istringstream stream(text);
	std::string raw;
	std::string section;
	while(std::getline(stream, raw))
	{
		if(!raw.empty() && raw.back() == '\r')
			raw.pop_back();
		Line line;
		line.raw = raw;
		std::string t = Trim(raw);
		if(!t.empty() && t.front() == '[' && t.back() == ']')
			section = Lower(Trim(t.substr(1, t.size() - 2)));
		line.section = section;
		if(!t.empty() && t.front() != ';' && t.front() != '#' && t.front() != '[')
		{
			size_t eq = t.find('=');
			if(eq != std::string::npos)
			{
				line.key = Lower(Trim(t.substr(0, eq)));
				line.value = StripInlineComment(t.substr(eq + 1));
			}
		}
		doc.lines.push_back(line);
	}
	return doc;
}

std::optional<std::string> IniDocument::Get(const std::string &section, const std::string &key) const
{
	std::string s = Lower(section), k = Lower(key);
	for(const auto &line : lines)
		if(line.section == s && line.key == k)
			return line.value;
	return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> IniDocument::Entries(const std::string &section) const
{
	std::vector<std::pair<std::string, std::string>> result;
	std::string s = Lower(section);
	for(const auto &line : lines)
		if(line.section == s && !line.key.empty())
			result.emplace_back(line.key, line.value);
	return result;
}

void IniDocument::Set(const std::string &section, const std::string &key, const std::string &value)
{
	std::string s = Lower(section), k = Lower(key);
	for(auto &line : lines)
	{
		if(line.section == s && line.key == k)
		{
			line.value = value;
			line.raw = key + " = " + value;
			return;
		}
	}

	Line line;
	line.section = s;
	line.key = k;
	line.value = value;
	line.raw = key + " = " + value;

	// insert after the last line of the existing section
	size_t insert_at = lines.size();
	bool found = false;
	for(size_t i = 0; i < lines.size(); i++)
	{
		if(lines[i].section == s)
		{
			found = true;
			if(!Trim(lines[i].raw).empty())
				insert_at = i + 1;
		}
	}
	if(!found)
	{
		Line header;
		header.raw = "[" + section + "]";
		header.section = s;
		lines.push_back(header);
		insert_at = lines.size();
	}
	lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), line);
}

std::string IniDocument::ToString() const
{
	std::string result;
	for(const auto &line : lines)
	{
		result += line.raw;
		result += '\n';
	}
	return result;
}

Profile ParseProfile(const std::string &text)
{
	Profile profile;
	IniDocument doc = IniDocument::Parse(text);

	auto read_double = [&](const char *key, double *out, double min, double max) {
		auto v = doc.Get("mouse", key);
		if(!v)
			return;
		double d;
		if(!ParseDouble(*v, &d))
		{
			profile.warnings.push_back(std::string("[mouse] ") + key + ": not a number: " + *v);
			return;
		}
		if(d < min || d > max)
		{
			profile.warnings.push_back(std::string("[mouse] ") + key + ": out of range, clamped: " + *v);
			d = std::clamp(d, min, max);
		}
		*out = d;
	};
	auto read_bool = [&](const char *section, const char *key, bool *out) {
		auto v = doc.Get(section, key);
		if(v && !ParseBool(*v, out))
			profile.warnings.push_back(std::string("[") + section + "] " + key + ": not a boolean: " + *v);
	};

	AimConfig &aim = profile.aim;
	read_double("sensitivity", &aim.sensitivity, 0.0, 100.0);
	read_double("exponent", &aim.exponent, 0.1, 5.0);
	read_double("anti_deadzone", &aim.anti_deadzone, 0.0, 0.9);
	read_double("max_output", &aim.max_output, 0.05, 1.0);
	read_double("smoothing_ms", &aim.smoothing_ms, 1.0, 200.0);
	read_double("y_scale", &aim.y_scale, 0.0, 10.0);
	read_double("ads_scale", &aim.ads_scale, 0.0, 10.0);
	read_double("walk_scale", &aim.walk_scale, 0.0, 1.0);
	read_double("update_interval_ms", &aim.update_interval_ms, 1.0, 50.0);
	read_bool("mouse", "invert_y", &aim.invert_y);
	read_bool("general", "click_to_capture", &profile.click_to_capture);

	for(const auto &[key, value] : doc.Entries("bindings"))
	{
		auto action = ParseActionName(key);
		if(!action)
		{
			profile.warnings.push_back("[bindings] unknown action: " + key);
			continue;
		}
		for(const auto &input : SplitList(value))
			profile.bindings.push_back({ input, *action });
	}
	return profile;
}

void AimMapper::Reset()
{
	samples.clear();
	acc_dx = acc_dy = 0.0;
}

void AimMapper::Update(double now_ms, bool ads, int16_t *out_x, int16_t *out_y)
{
	if(acc_dx != 0.0 || acc_dy != 0.0)
		samples.push_back({ now_ms, acc_dx, acc_dy });
	acc_dx = acc_dy = 0.0;

	const double window = std::max(config.smoothing_ms, 1.0);
	while(!samples.empty() && samples.front().t <= now_ms - window)
		samples.pop_front();

	double sum_x = 0.0, sum_y = 0.0;
	for(const auto &s : samples)
	{
		sum_x += s.dx;
		sum_y += s.dy;
	}

	// mouse speed in counts per millisecond
	double vx = sum_x / window;
	double vy = sum_y / window * config.y_scale;
	if(config.invert_y)
		vy = -vy;

	double speed = std::hypot(vx, vy);
	if(speed < 1e-9)
	{
		*out_x = 0;
		*out_y = 0;
		return;
	}

	double scaled = speed * config.sensitivity * (ads ? config.ads_scale : 1.0);
	double magnitude = config.anti_deadzone + std::pow(scaled, config.exponent);
	magnitude = std::min(magnitude, config.max_output);

	*out_x = ToAxis(vx / speed * magnitude);
	*out_y = ToAxis(vy / speed * magnitude);
}

PadOutput ComputePad(const std::array<bool, ActionCount> &held, double walk_scale)
{
	auto h = [&](Action a) { return held[static_cast<size_t>(a)]; };
	PadOutput out;

	struct { Action action; uint32_t bit; } buttons[] = {
		{ Action::Cross, CHIAKI_CONTROLLER_BUTTON_CROSS },
		{ Action::Circle, CHIAKI_CONTROLLER_BUTTON_MOON },
		{ Action::Square, CHIAKI_CONTROLLER_BUTTON_BOX },
		{ Action::Triangle, CHIAKI_CONTROLLER_BUTTON_PYRAMID },
		{ Action::L1, CHIAKI_CONTROLLER_BUTTON_L1 },
		{ Action::R1, CHIAKI_CONTROLLER_BUTTON_R1 },
		{ Action::L3, CHIAKI_CONTROLLER_BUTTON_L3 },
		{ Action::R3, CHIAKI_CONTROLLER_BUTTON_R3 },
		{ Action::Options, CHIAKI_CONTROLLER_BUTTON_OPTIONS },
		{ Action::Create, CHIAKI_CONTROLLER_BUTTON_SHARE },
		{ Action::Touchpad, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD },
		{ Action::PS, CHIAKI_CONTROLLER_BUTTON_PS },
		{ Action::DpadUp, CHIAKI_CONTROLLER_BUTTON_DPAD_UP },
		{ Action::DpadDown, CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN },
		{ Action::DpadLeft, CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT },
		{ Action::DpadRight, CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT },
	};
	for(const auto &b : buttons)
		if(h(b.action))
			out.buttons |= b.bit;

	out.l2 = h(Action::L2) ? 0xff : 0;
	out.r2 = h(Action::R2) ? 0xff : 0;

	// Opposite directions cancel out; diagonals are normalized to the unit circle.
	auto stick = [](bool up, bool down, bool left, bool right, double scale, int16_t *x, int16_t *y) {
		double fx = (right ? 1.0 : 0.0) - (left ? 1.0 : 0.0);
		double fy = (down ? 1.0 : 0.0) - (up ? 1.0 : 0.0);
		double len = std::hypot(fx, fy);
		if(len > 1.0)
		{
			fx /= len;
			fy /= len;
		}
		*x = ToAxis(fx * scale);
		*y = ToAxis(fy * scale);
	};
	stick(h(Action::LsUp), h(Action::LsDown), h(Action::LsLeft), h(Action::LsRight),
			h(Action::Walk) ? walk_scale : 1.0, &out.left_x, &out.left_y);
	stick(h(Action::RsUp), h(Action::RsDown), h(Action::RsLeft), h(Action::RsRight),
			1.0, &out.right_x, &out.right_y);
	return out;
}

const char *DefaultProfileText()
{
	return R"INI(; Профиль клавиатуры и мыши для chiaki-ng (режим прицеливания мышью).
;
; F8  - захватить / отпустить мышь (в захвате работает прицеливание)
; F9  - чувствительность -10%,  F10 - чувствительность +10% (сохраняется в этот файл)
; Левый клик по окну стрима тоже захватывает мышь (см. click_to_capture).
; Изменения в файле применяются при следующем захвате мыши (F8 два раза).
;
; Раскладка ниже - стартовая, в стиле GTA. Проверьте её по схеме управления
; в настройках самой игры и поменяйте под себя.

[general]
click_to_capture = true

[mouse]
; Отклонение стика (0..1) на 1 отсчёт мыши в миллисекунду. Главный параметр.
sensitivity = 0.12
; Кривая отклика: 1.0 - линейная, больше 1 - точнее на малых движениях.
exponent = 1.0
; Минимальное отклонение стика при любом движении мыши (перепрыгивает мёртвую зону игры).
; Если камера "залипает" на медленных движениях - увеличьте; если дрожит - уменьшите.
anti_deadzone = 0.18
max_output = 1.0
; Окно сглаживания скорости мыши, мс. Больше - плавнее, но камера дольше "доезжает".
smoothing_ms = 12
; Множитель вертикали и инверсия.
y_scale = 1.0
invert_y = false
; Множитель чувствительности, пока зажат L2 (прицеливание).
ads_scale = 1.0
; Отклонение левого стика при зажатом модификаторе "walk" (шаг).
walk_scale = 0.45
update_interval_ms = 4

[bindings]
; действие = клавиша[, клавиша ...]
; Клавиши: A..Z, 0..9, F1..F12, Space, Tab, Enter, Esc, Backspace, Shift, Ctrl, Alt,
;          CapsLock, Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown
; Мышь:    MouseLeft, MouseRight, MouseMiddle, MouseBack, MouseForward, WheelUp, WheelDown
; F8, F9, F10, F11 заняты служебными функциями.
;
; Действия: cross circle square triangle l1 r1 l2 r2 l3 r3 options create touchpad ps
;           dpad_up dpad_down dpad_left dpad_right ls_up ls_down ls_left ls_right
;           rs_up rs_down rs_left rs_right walk

ls_up = W
ls_down = S
ls_left = A
ls_right = D
; Alt лучше не использовать: в Windows отпускание Alt может активировать меню окна.
walk = CapsLock

r2 = MouseLeft
l2 = MouseRight
r3 = MouseMiddle, C
l3 = Ctrl

cross = Shift
square = Space
triangle = F
circle = R
r1 = Q
l1 = Tab

dpad_up = Up, MouseForward
dpad_down = Down, MouseBack
dpad_left = Left, WheelUp
dpad_right = Right, WheelDown

touchpad = M
options = Esc, P
create = F12
ps = Home

; камера с клавиатуры (на всякий случай)
rs_up = I
rs_down = K
rs_left = J
rs_right = L
)INI";
}

} // namespace kbm
