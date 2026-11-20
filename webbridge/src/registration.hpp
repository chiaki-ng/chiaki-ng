// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include "config.hpp"

#include <array>
#include <cstdint>
#include <string>

// Blocking console pairing (chiaki_regist). Returns true and fills `out` on
// success; on failure returns false with a human-readable error.
bool register_console(const std::string &host, bool ps5, uint32_t pin,
		const std::array<uint8_t, 8> &psn_account_id,
		ConsoleConfig &out, std::string &error);
