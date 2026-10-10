// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "config.hpp"
#include "log.hpp"

#include <chiaki/base64.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

using nlohmann::json;
namespace fs = std::filesystem;

static std::string default_config_path()
{
	const char *xdg = std::getenv("XDG_CONFIG_HOME");
	std::string base = xdg && *xdg ? xdg : std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.config";
	return base + "/chiaki-web/config.json";
}

static std::string hex_encode(const uint8_t *buf, size_t size)
{
	static const char *d = "0123456789abcdef";
	std::string s;
	s.reserve(size * 2);
	for(size_t i = 0; i < size; i++)
	{
		s += d[buf[i] >> 4];
		s += d[buf[i] & 0xf];
	}
	return s;
}

static bool hex_decode(const std::string &s, uint8_t *buf, size_t size)
{
	if(s.size() != size * 2)
		return false;
	auto nib = [](char c) -> int {
		if(c >= '0' && c <= '9') return c - '0';
		if(c >= 'a' && c <= 'f') return c - 'a' + 10;
		if(c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	for(size_t i = 0; i < size; i++)
	{
		int hi = nib(s[i * 2]), lo = nib(s[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return false;
		buf[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

Config::Config(std::string path_override)
	: path(path_override.empty() ? default_config_path() : std::move(path_override))
{
}

void Config::load_psn_account_id_from_env()
{
	const char *b64 = std::getenv("CHIAKI_PSN_ACCOUNT_ID");
	if(!b64 || !*b64)
		return;
	size_t sz = psn_account_id.size();
	if(chiaki_base64_decode(b64, strlen(b64), psn_account_id.data(), &sz) == CHIAKI_ERR_SUCCESS && sz == psn_account_id.size())
	{
		psn_account_id_set = true;
		CHIAKI_LOGI(bridge_log(), "webbridge: PSN account id taken from CHIAKI_PSN_ACCOUNT_ID");
	}
	else
		CHIAKI_LOGE(bridge_log(), "webbridge: CHIAKI_PSN_ACCOUNT_ID is not valid base64 of 8 bytes, ignoring");
}

void Config::load()
{
	std::lock_guard<std::mutex> lock(mutex);
	consoles.clear();
	psn_account_id_set = false;
	std::ifstream f(path);
	if(!f.good())
	{
		load_psn_account_id_from_env();
		return;
	}
	json j;
	try
	{
		f >> j;
	}
	catch(const std::exception &e)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: failed to parse config %s: %s", path.c_str(), e.what());
		return;
	}
	if(j.contains("psnAccountId") && j["psnAccountId"].is_string())
	{
		std::string b64 = j["psnAccountId"];
		size_t sz = psn_account_id.size();
		if(!b64.empty() && chiaki_base64_decode(b64.c_str(), b64.size(), psn_account_id.data(), &sz) == CHIAKI_ERR_SUCCESS && sz == psn_account_id.size())
			psn_account_id_set = true;
	}
	if(!psn_account_id_set)
		load_psn_account_id_from_env();
	for(const auto &jc : j.value("consoles", json::array()))
	{
		ConsoleConfig c;
		c.host = jc.value("host", "");
		c.nickname = jc.value("nickname", "");
		c.ps5 = jc.value("ps5", true);
		if(c.host.empty()
			|| !hex_decode(jc.value("registKey", ""), c.regist_key.data(), c.regist_key.size())
			|| !hex_decode(jc.value("rpKey", ""), c.rp_key.data(), c.rp_key.size()))
		{
			CHIAKI_LOGE(bridge_log(), "webbridge: skipping malformed console entry in config");
			continue;
		}
		consoles.push_back(std::move(c));
	}
	CHIAKI_LOGI(bridge_log(), "webbridge: loaded config with %zu console(s) from %s", consoles.size(), path.c_str());
}

void Config::save()
{
	std::lock_guard<std::mutex> lock(mutex);
	json j;
	if(psn_account_id_set)
	{
		char b64[32] = {};
		if(chiaki_base64_encode(psn_account_id.data(), psn_account_id.size(), b64, sizeof(b64)) == CHIAKI_ERR_SUCCESS)
			j["psnAccountId"] = b64;
	}
	j["consoles"] = json::array();
	for(const auto &c : consoles)
	{
		j["consoles"].push_back({
			{"host", c.host},
			{"nickname", c.nickname},
			{"ps5", c.ps5},
			{"registKey", hex_encode(c.regist_key.data(), c.regist_key.size())},
			{"rpKey", hex_encode(c.rp_key.data(), c.rp_key.size())},
		});
	}
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::ofstream f(path);
	f << j.dump(1, '\t') << "\n";
	if(!f.good())
		CHIAKI_LOGE(bridge_log(), "webbridge: failed to write config %s", path.c_str());
	else
		CHIAKI_LOGI(bridge_log(), "webbridge: saved config %s", path.c_str());
}

const ConsoleConfig *Config::find_console(const std::string &host) const
{
	for(const auto &c : consoles)
		if(c.host == host)
			return &c;
	return nullptr;
}

void Config::upsert_console(const ConsoleConfig &nc)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		bool found = false;
		for(auto &c : consoles)
		{
			// Same console re-registered (match by nickname) or same address
			if(c.host == nc.host || (!nc.nickname.empty() && c.nickname == nc.nickname))
			{
				c = nc;
				found = true;
				break;
			}
		}
		if(!found)
			consoles.push_back(nc);
	}
	save();
}

nlohmann::json Config::sanitized() const
{
	json j;
	j["psnAccountIdSet"] = psn_account_id_set;
	j["consoles"] = json::array();
	for(const auto &c : consoles)
		j["consoles"].push_back({{"host", c.host}, {"nickname", c.nickname}, {"ps5", c.ps5}});
	return j;
}
