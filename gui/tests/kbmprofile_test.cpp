// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Standalone tests for the keyboard/mouse profile and aim mapper.

#include <kbmprofile.h>

#include <chiaki/controller.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

#define CHECK(cond) do { if(!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

using namespace kbm;

static void TestDefaultProfileParses()
{
	Profile p = ParseProfile(DefaultProfileText());
	for(const auto &w : p.warnings)
		std::fprintf(stderr, "warning: %s\n", w.c_str());
	CHECK(p.warnings.empty());
	CHECK(p.click_to_capture);
	CHECK(std::fabs(p.aim.sensitivity - 0.12) < 1e-9);
	CHECK(!p.bindings.empty());

	bool found_r2_mouse = false, found_r3_c = false;
	for(const auto &b : p.bindings)
	{
		if(b.action == Action::R2 && b.input == "MouseLeft")
			found_r2_mouse = true;
		if(b.action == Action::R3 && b.input == "C")
			found_r3_c = true;
	}
	CHECK(found_r2_mouse);
	CHECK(found_r3_c);
}

static void TestProfileWarningsAndClamping()
{
	Profile p = ParseProfile(
		"[mouse]\n"
		"sensitivity = abc\n"
		"anti_deadzone = 5 ; too big\n"
		"invert_y = yes\n"
		"[bindings]\n"
		"jump = Space\n"
		"Cross = Space ,  Enter\n");
	CHECK(p.warnings.size() == 3);
	CHECK(std::fabs(p.aim.sensitivity - 0.12) < 1e-9); // unchanged default
	CHECK(std::fabs(p.aim.anti_deadzone - 0.9) < 1e-9); // clamped
	CHECK(p.aim.invert_y);
	CHECK(p.bindings.size() == 2);
	CHECK(p.bindings[0].action == Action::Cross && p.bindings[0].input == "Space");
	CHECK(p.bindings[1].input == "Enter");
}

static void TestIniSetPreservesComments()
{
	IniDocument doc = IniDocument::Parse("; header\r\n[mouse]\r\n; comment\r\nsensitivity = 0.1\r\n\r\n[bindings]\r\ncross = Space\r\n");
	doc.Set("mouse", "sensitivity", "0.2");
	doc.Set("mouse", "exponent", "1.5");
	doc.Set("general", "click_to_capture", "false");
	std::string out = doc.ToString();
	CHECK(out.find("; header") != std::string::npos);
	CHECK(out.find("; comment") != std::string::npos);
	CHECK(out.find("sensitivity = 0.2") != std::string::npos);
	CHECK(out.find("0.1") == std::string::npos);
	// new key lands inside [mouse], before [bindings]
	CHECK(out.find("exponent = 1.5") < out.find("[bindings]"));
	CHECK(out.find("[general]\nclick_to_capture = false") != std::string::npos);

	IniDocument again = IniDocument::Parse(out);
	CHECK(again.Get("MOUSE", "Sensitivity").value_or("") == "0.2");
	CHECK(again.Get("bindings", "cross").value_or("") == "Space");
}

static void TestAimMapperIdleIsCentered()
{
	AimMapper m;
	int16_t x = 1, y = 1;
	m.Update(0.0, false, &x, &y);
	CHECK(x == 0 && y == 0);
}

static void TestAimMapperDirectionAndAntiDeadzone()
{
	AimConfig c;
	c.sensitivity = 0.1;
	c.anti_deadzone = 0.2;
	c.smoothing_ms = 10.0;
	AimMapper m;
	m.Configure(c);

	int16_t x, y;
	// one count to the right: speed 0.1 counts/ms -> 0.2 + 0.01 = 0.21
	m.AddDelta(1.0, 0.0);
	m.Update(100.0, false, &x, &y);
	CHECK(std::abs(x - static_cast<int>(std::lround(0.21 * 32767))) <= 1);
	CHECK(y == 0);

	// mouse down -> stick down (positive y), up -> negative
	m.Reset();
	m.AddDelta(0.0, 5.0);
	m.Update(200.0, false, &x, &y);
	CHECK(x == 0 && y > 0);
	m.Reset();
	m.AddDelta(0.0, -5.0);
	m.Update(300.0, false, &x, &y);
	CHECK(y < 0);
}

static void TestAimMapperReturnsToCenterAfterWindow()
{
	AimConfig c;
	c.smoothing_ms = 12.0;
	AimMapper m;
	m.Configure(c);
	int16_t x, y;
	m.AddDelta(30.0, 0.0);
	m.Update(1000.0, false, &x, &y);
	CHECK(x > 0);
	m.Update(1008.0, false, &x, &y);
	CHECK(x > 0); // still inside the smoothing window
	m.Update(1012.5, false, &x, &y);
	CHECK(x == 0 && y == 0);
}

static void TestAimMapperSaturatesAndKeepsDirection()
{
	AimConfig c;
	c.max_output = 0.9;
	AimMapper m;
	m.Configure(c);
	int16_t x, y;
	m.AddDelta(1000.0, 1000.0);
	m.Update(0.0, false, &x, &y);
	double mag = std::hypot(x / 32767.0, y / 32767.0);
	CHECK(std::fabs(mag - 0.9) < 0.001);
	CHECK(x == y);
}

static void TestAimMapperInvertAndAds()
{
	AimConfig c;
	c.invert_y = true;
	c.anti_deadzone = 0.0;
	c.sensitivity = 0.1;
	c.ads_scale = 0.5;
	c.smoothing_ms = 10.0;
	AimMapper m;
	m.Configure(c);
	int16_t x, y, ax, ay;
	m.AddDelta(0.0, 10.0);
	m.Update(0.0, false, &x, &y);
	CHECK(y < 0);
	m.Reset();
	m.AddDelta(0.0, 10.0);
	m.Update(100.0, true, &ax, &ay);
	CHECK(std::abs(ay * 2 - y) <= 2);
}

static void TestComputePad()
{
	std::array<bool, ActionCount> held {};
	auto set = [&](Action a, bool v) { held[static_cast<size_t>(a)] = v; };

	PadOutput o = ComputePad(held, 0.5);
	CHECK(o.buttons == 0 && o.left_x == 0 && o.left_y == 0 && o.l2 == 0);

	set(Action::Cross, true);
	set(Action::R2, true);
	set(Action::LsUp, true);
	o = ComputePad(held, 0.5);
	CHECK(o.buttons == CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK(o.r2 == 0xff && o.l2 == 0);
	CHECK(o.left_y == -32767 && o.left_x == 0);

	// opposite keys cancel out instead of "last key wins"
	set(Action::LsLeft, true);
	set(Action::LsRight, true);
	o = ComputePad(held, 0.5);
	CHECK(o.left_x == 0 && o.left_y == -32767);

	// diagonal is normalized
	set(Action::LsLeft, false);
	o = ComputePad(held, 0.5);
	CHECK(std::fabs(std::hypot(o.left_x / 32767.0, o.left_y / 32767.0) - 1.0) < 0.001);

	// walk modifier scales the left stick
	set(Action::LsRight, false);
	set(Action::Walk, true);
	o = ComputePad(held, 0.5);
	CHECK(std::abs(o.left_y + 16384) <= 1);
}

int main()
{
	TestDefaultProfileParses();
	TestProfileWarningsAndClamping();
	TestIniSetPreservesComments();
	TestAimMapperIdleIsCentered();
	TestAimMapperDirectionAndAntiDeadzone();
	TestAimMapperReturnsToCenterAfterWindow();
	TestAimMapperSaturatesAndKeepsDirection();
	TestAimMapperInvertAndAds();
	TestComputePad();
	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	std::printf("kbmprofile tests passed\n");
	return EXIT_SUCCESS;
}
