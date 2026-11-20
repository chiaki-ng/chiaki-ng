// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "discoverymgr.hpp"
#include "log.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cstdlib>
#include <cstring>

DiscoveryManager::DiscoveryManager() = default;

DiscoveryManager::~DiscoveryManager()
{
	stop();
}

void DiscoveryManager::discovery_cb(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user)
{
	auto *self = static_cast<DiscoveryManager *>(user);
	std::vector<DiscoveredHost> list;
	list.reserve(hosts_count);
	for(size_t i = 0; i < hosts_count; i++)
	{
		ChiakiDiscoveryHost &h = hosts[i];
		DiscoveredHost d;
		d.host_addr = h.host_addr ? h.host_addr : "";
		d.host_name = h.host_name ? h.host_name : "";
		d.host_id = h.host_id ? h.host_id : "";
		d.app_name = h.running_app_name ? h.running_app_name : "";
		d.ps5 = chiaki_discovery_host_is_ps5(&h);
		d.ready = h.state == CHIAKI_DISCOVERY_HOST_STATE_READY;
		d.standby = h.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;
		list.push_back(std::move(d));
	}
	std::lock_guard<std::mutex> lock(self->mutex);
	self->hosts_ = std::move(list);
}

bool DiscoveryManager::start(const std::vector<std::string> &extra_unicast)
{
	if(active)
		return true;

	static struct sockaddr_in send_addr;
	memset(&send_addr, 0, sizeof(send_addr));
	send_addr.sin_family = AF_INET;
	send_addr.sin_addr.s_addr = INADDR_BROADCAST;

	ChiakiDiscoveryServiceOptions options = {};
	options.hosts_max = 16;
	options.host_drop_pings = 3;
	options.ping_ms = 500;
	options.ping_initial_ms = 100;
	options.send_addr = (struct sockaddr_storage *)&send_addr;
	options.send_addr_size = sizeof(send_addr);
	options.send_host = nullptr;
	options.cb = discovery_cb;
	options.cb_user = this;

	// per-interface directed broadcasts (mirrors the Qt GUI)
	static std::vector<struct sockaddr_storage> broadcast_addrs;
	broadcast_addrs.clear();
	struct ifaddrs *ifs = nullptr;
	if(getifaddrs(&ifs) == 0)
	{
		for(struct ifaddrs *cur = ifs; cur; cur = cur->ifa_next)
		{
			if(!cur->ifa_addr || cur->ifa_addr->sa_family != AF_INET)
				continue;
			if((cur->ifa_flags & (IFF_UP | IFF_RUNNING | IFF_LOOPBACK | IFF_BROADCAST)) != (IFF_UP | IFF_RUNNING | IFF_BROADCAST))
				continue;
			if(!cur->ifa_broadaddr)
				continue;
			struct sockaddr_storage ss = {};
			memcpy(&ss, cur->ifa_broadaddr, sizeof(struct sockaddr_in));
			broadcast_addrs.push_back(ss);
		}
		freeifaddrs(ifs);
	}
	for(const auto &host : extra_unicast)
	{
		struct sockaddr_in sin = {};
		sin.sin_family = AF_INET;
		if(inet_pton(AF_INET, host.c_str(), &sin.sin_addr) == 1)
		{
			struct sockaddr_storage ss = {};
			memcpy(&ss, &sin, sizeof(sin));
			broadcast_addrs.push_back(ss);
		}
	}
	if(!broadcast_addrs.empty())
	{
		options.broadcast_addrs = broadcast_addrs.data();
		options.broadcast_num = broadcast_addrs.size();
	}

	ChiakiErrorCode err = chiaki_discovery_service_init(&service, &options, bridge_log());
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(bridge_log(), "webbridge: discovery service init failed: %s", chiaki_error_string(err));
		return false;
	}
	active = true;
	CHIAKI_LOGI(bridge_log(), "webbridge: discovery service running (%zu broadcast addrs)", broadcast_addrs.size());
	return true;
}

void DiscoveryManager::stop()
{
	if(!active)
		return;
	chiaki_discovery_service_fini(&service);
	active = false;
}

std::vector<DiscoveredHost> DiscoveryManager::hosts() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return hosts_;
}

bool DiscoveryManager::send_wakeup(const std::string &host, const std::string &regist_key_hex, bool ps5, std::string &error)
{
	uint64_t credential = strtoull(regist_key_hex.c_str(), nullptr, 16);
	ChiakiErrorCode err = chiaki_discovery_wakeup(bridge_log(), active ? &service.discovery : nullptr,
			host.c_str(), credential, ps5);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		error = chiaki_error_string(err);
		return false;
	}
	return true;
}
