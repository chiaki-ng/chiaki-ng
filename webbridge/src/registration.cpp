// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "registration.hpp"
#include "log.hpp"

#include <chiaki/regist.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace
{

struct RegistWait
{
	std::mutex mutex;
	std::condition_variable cv;
	bool done = false;
	bool success = false;
	ChiakiRegisteredHost host{};
};

void regist_cb(ChiakiRegistEvent *event, void *user)
{
	auto *wait = static_cast<RegistWait *>(user);
	std::lock_guard<std::mutex> lock(wait->mutex);
	switch(event->type)
	{
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
			wait->success = true;
			if(event->registered_host)
				wait->host = *event->registered_host;
			break;
		default:
			wait->success = false;
			break;
	}
	wait->done = true;
	wait->cv.notify_all();
}

} // namespace

bool register_console(const std::string &host, bool ps5, uint32_t pin,
		const std::array<uint8_t, 8> &psn_account_id,
		ConsoleConfig &out, std::string &error)
{
	ChiakiRegistInfo info = {};
	info.target = ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
	info.host = host.c_str();
	info.broadcast = false;
	info.psn_online_id = nullptr;
	memcpy(info.psn_account_id, psn_account_id.data(), sizeof(info.psn_account_id));
	info.pin = pin;
	info.console_pin = 0;
	info.holepunch_info = nullptr;
	info.rudp = nullptr;

	RegistWait wait;
	ChiakiRegist regist;
	ChiakiErrorCode err = chiaki_regist_start(&regist, bridge_log(), &info, regist_cb, &wait);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		error = std::string("regist start failed: ") + chiaki_error_string(err);
		return false;
	}

	bool finished;
	{
		std::unique_lock<std::mutex> lock(wait.mutex);
		finished = wait.cv.wait_for(lock, std::chrono::seconds(45), [&wait] { return wait.done; });
	}
	if(!finished)
		chiaki_regist_stop(&regist);
	chiaki_regist_fini(&regist);

	if(!finished)
	{
		error = "registration timed out";
		return false;
	}
	if(!wait.success)
	{
		error = "registration failed (check PIN, PSN Account ID, and that Remote Play pairing is open on the console)";
		return false;
	}

	out.host = host;
	out.ps5 = chiaki_target_is_ps5(wait.host.target);
	out.nickname = std::string(wait.host.server_nickname,
			strnlen(wait.host.server_nickname, sizeof(wait.host.server_nickname)));
	static_assert(sizeof(wait.host.rp_regist_key) == 16, "regist key size");
	memcpy(out.regist_key.data(), wait.host.rp_regist_key, out.regist_key.size());
	memcpy(out.rp_key.data(), wait.host.rp_key, out.rp_key.size());
	return true;
}
