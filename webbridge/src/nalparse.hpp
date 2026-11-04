// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Minimal Annex-B scanning: enough to flag keyframes / parameter sets for the
// fragment header. Codec-agnostic decode stays in the browser.

struct NalScanResult
{
	bool keyframe = false;   // IDR/CRA slice present
	bool parameter_sets = false; // SPS/PPS(/VPS) present
};

// Walk NAL units; calls cb(nal_type, nal_start_offset_incl_startcode, nal_off).
template<typename F>
inline void foreach_nal(const uint8_t *buf, size_t size, bool hevc, F cb)
{
	size_t i = 0;
	while(i + 3 < size)
	{
		if(buf[i] == 0 && buf[i + 1] == 0)
		{
			size_t nal_off = 0;
			if(buf[i + 2] == 1)
				nal_off = i + 3;
			else if(i + 4 < size && buf[i + 2] == 0 && buf[i + 3] == 1)
				nal_off = i + 4;
			if(nal_off && nal_off < size)
			{
				uint8_t type = hevc ? (uint8_t)((buf[nal_off] >> 1) & 0x3f) : (uint8_t)(buf[nal_off] & 0x1f);
				cb(type, i, nal_off);
				i = nal_off;
				continue;
			}
		}
		i++;
	}
}

inline bool nal_is_keyframe(uint8_t type, bool hevc)
{
	return hevc ? (type >= 16 && type <= 21) /* BLA/IDR/CRA */ : type == 5;
}

inline bool nal_is_param_set(uint8_t type, bool hevc)
{
	return hevc ? (type >= 32 && type <= 34) /* VPS/SPS/PPS */ : (type == 7 || type == 8);
}

inline NalScanResult scan_annexb(const uint8_t *buf, size_t size, bool hevc)
{
	NalScanResult res;
	foreach_nal(buf, size, hevc, [&](uint8_t type, size_t, size_t) {
		if(nal_is_keyframe(type, hevc))
			res.keyframe = true;
		if(nal_is_param_set(type, hevc))
			res.parameter_sets = true;
	});
	return res;
}

// Extract all parameter-set NALs (with 4-byte start codes) from an AU, e.g.
// to cache them and prepend to keyframes for clients that joined mid-stream.
inline std::vector<uint8_t> extract_param_sets(const uint8_t *buf, size_t size, bool hevc)
{
	std::vector<uint8_t> out;
	// collect [start, end) ranges of param-set NALs; end = next NAL start or AU end
	size_t pending_start = SIZE_MAX;
	auto flush = [&](size_t end) {
		if(pending_start == SIZE_MAX)
			return;
		static const uint8_t sc[4] = {0, 0, 0, 1};
		out.insert(out.end(), sc, sc + 4);
		// skip the original start code (3 or 4 bytes)
		size_t s = pending_start;
		size_t skip = (buf[s + 2] == 1) ? 3 : 4;
		out.insert(out.end(), buf + s + skip, buf + end);
		pending_start = SIZE_MAX;
	};
	foreach_nal(buf, size, hevc, [&](uint8_t type, size_t start, size_t) {
		flush(start);
		if(nal_is_param_set(type, hevc))
			pending_start = start;
	});
	flush(size);
	return out;
}
