// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <chiaki/log.h>

// Single process-wide chiaki logger printing to stdout.
inline ChiakiLog *bridge_log()
{
	static ChiakiLog log = []() {
		ChiakiLog l;
		chiaki_log_init(&l, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE & ~CHIAKI_LOG_DEBUG, chiaki_log_cb_print, nullptr);
		return l;
	}();
	return &log;
}

inline void bridge_log_set_verbose(bool verbose)
{
	bridge_log()->level_mask = verbose ? (CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE) : (CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE & ~CHIAKI_LOG_DEBUG);
}
