// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <chiaki/discoveryservice.h>

#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <vector>

struct DiscoveredHost
{
	std::string host_addr;
	std::string host_name;
	std::string host_id; // MAC
	std::string app_name;
	bool ps5 = false;
	bool ready = false;
	bool standby = false;
};

// Continuous LAN discovery (UDP broadcast ping to PS4/PS5 ports).
class DiscoveryManager
{
public:
	DiscoveryManager();
	~DiscoveryManager();

	// extra_unicast: addresses (registered consoles) pinged directly in
	// addition to the per-interface broadcasts
	bool start(const std::vector<std::string> &extra_unicast = {});
	void stop();

	std::vector<DiscoveredHost> hosts() const;

	// One-shot wakeup packet. regist_key is the ASCII-hex credential.
	bool send_wakeup(const std::string &host, const std::string &regist_key_hex, bool ps5, std::string &error);

private:
	static void discovery_cb(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user);

	ChiakiDiscoveryService service{};
	bool active = false;

	mutable std::mutex mutex;
	std::vector<DiscoveredHost> hosts_;
};
