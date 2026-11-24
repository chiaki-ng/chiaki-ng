// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_MOUSESTICK_H
#define CHIAKI_MOUSESTICK_H

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * Converts relative mouse motion into an analog stick deflection.
 *
 * Mouse movement is a stream of position deltas, while a console game reads a
 * stick position every frame and turns it into a camera rotation speed. So the
 * mouse velocity (pixels per millisecond) is mapped to stick deflection:
 *
 *   deflection = deadzone + (1 - deadzone) * min(1, (speed * gain) ^ curve)
 *
 * "deadzone" is an anti-deadzone: console games ignore small stick deflections,
 * so any real movement starts just past the game's inner deadzone, otherwise slow
 * mouse movement would do nothing. Velocity is smoothed with an exponential moving
 * average so a mouse polling at 125-8000 Hz produces a steady stick value instead
 * of jitter, and the stick returns to center when the mouse stops.
 *
 * This class has no Qt dependency so it can be unit tested on its own.
 */
class MouseStickMapper
{
	public:
		struct Params
		{
			// 1..100, 50 means full deflection at ~1 px/ms (a fast but normal flick)
			int sensitivity = 40;
			// 0..90 percent of the stick range that the game ignores
			int deadzone = 15;
			// response curve exponent, 1.0 = linear, < 1 boosts small movements
			double curve = 1.0;
			// weight of the newest sample in the moving average, 0..1
			double smoothing = 0.5;
			bool invert_y = false;
		};

		void SetParams(const Params &p) { params = p; }
		const Params &GetParams() const { return params; }

		void AddDelta(double dx, double dy) { acc_x += dx; acc_y += dy; }

		void Reset()
		{
			acc_x = acc_y = 0.0;
			vel_x = vel_y = 0.0;
			out_x = out_y = 0;
		}

		/**
		 * Consume the motion collected since the last tick.
		 * @param dt_ms time since the previous tick in milliseconds
		 * @return true if the stick value changed
		 */
		bool Tick(double dt_ms)
		{
			if(dt_ms <= 0.0)
				return false;
			// a stalled timer (e.g. window dragged) must not create a huge spike or a stale value
			dt_ms = std::min(dt_ms, 50.0);

			double a = std::clamp(params.smoothing, 0.05, 1.0);
			vel_x = a * (acc_x / dt_ms) + (1.0 - a) * vel_x;
			vel_y = a * (acc_y / dt_ms) + (1.0 - a) * vel_y;
			acc_x = acc_y = 0.0;

			double gain = std::clamp(params.sensitivity, 1, 100) / 50.0;
			double speed = std::hypot(vel_x, vel_y) * gain;

			int16_t nx = 0, ny = 0;
			if(speed < kIdleThreshold)
			{
				vel_x = vel_y = 0.0;
			}
			else
			{
				double dz = std::clamp(params.deadzone, 0, 90) / 100.0;
				double curve = std::clamp(params.curve, 0.2, 3.0);
				double mag = dz + (1.0 - dz) * std::min(1.0, std::pow(speed, curve));
				// keep the direction of motion, set the length to the mapped magnitude
				// (a real stick has a circular range, so |(x, y)| <= 1)
				double norm = std::hypot(vel_x, vel_y);
				nx = ToAxis(vel_x / norm * mag);
				ny = ToAxis(vel_y / norm * mag);
				if(params.invert_y)
					ny = static_cast<int16_t>(-ny);
			}

			bool changed = nx != out_x || ny != out_y;
			out_x = nx;
			out_y = ny;
			return changed;
		}

		int16_t GetX() const { return out_x; }
		int16_t GetY() const { return out_y; }
		bool IsActive() const { return out_x != 0 || out_y != 0 || vel_x != 0.0 || vel_y != 0.0; }

	private:
		static constexpr double kIdleThreshold = 0.004;

		static int16_t ToAxis(double v)
		{
			v = std::clamp(v, -1.0, 1.0);
			return static_cast<int16_t>(std::lround(v * 0x7fff));
		}

		Params params;
		double acc_x = 0.0, acc_y = 0.0;
		double vel_x = 0.0, vel_y = 0.0;
		int16_t out_x = 0, out_y = 0;
};

#endif // CHIAKI_MOUSESTICK_H
