// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// A console the bridge is registered (paired) with.
struct ConsoleConfig
{
	std::string host;
	std::string nickname;
	bool ps5 = true;
	// ASCII regist key as chiaki wants it in ChiakiConnectInfo::regist_key
	// (16 bytes, NUL-padded)
	std::array<uint8_t, 16> regist_key{};
	// "morning" / RP key
	std::array<uint8_t, 16> rp_key{};
};

class Config
{
public:
	explicit Config(std::string path_override = "");

	void load();
	void save();

	std::vector<ConsoleConfig> consoles;         // guarded by mutex
	std::array<uint8_t, 8> psn_account_id{};     // decoded from base64
	bool psn_account_id_set = false;

	const ConsoleConfig *find_console(const std::string &host) const;
	void upsert_console(const ConsoleConfig &c);

	nlohmann::json sanitized() const;

	// Fallback: CHIAKI_PSN_ACCOUNT_ID env var (populated from .env by main)
	void load_psn_account_id_from_env();

	std::mutex mutex;

private:
	std::string path;
};
