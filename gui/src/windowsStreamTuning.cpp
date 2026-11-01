// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "windowsStreamTuning.h"

#include <chiaki/thread.h>

#include <windows.h>
#include <avrt.h>
#include <timeapi.h>

#include <atomic>

void setWindowsStreamActivity(bool active)
{
	static std::atomic<bool> current{false};
	if (current.exchange(active) == active)
		return;

	// Windows 11 lets a process lose its timer resolution and run on the
	// efficiency cores (EcoQoS) once its window is hidden or not in front;
	// the stream would then wake late in 15.6 ms steps. Opt out while it runs.
	PROCESS_POWER_THROTTLING_STATE throttling = {};
	throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
	throttling.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#endif
	// StateMask 0 with the bit in ControlMask = "never throttle"; ControlMask
	// 0 hands the decision back to the system.
	if (!active)
		throttling.ControlMask = 0;
	throttling.StateMask = 0;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));

	if (active)
		timeBeginPeriod(1);
	else
		timeEndPeriod(1);
}

void setCurrentThreadGamePriority()
{
	DWORD task_index = 0;
	HANDLE task = AvSetMmThreadCharacteristicsW(L"Games", &task_index);
	if (task)
		AvSetMmThreadPriority(task, AVRT_PRIORITY_HIGH);
	else
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	// MMCSS lets go of the registration when the thread ends.
}

static void streamThreadPriority(ChiakiThreadName name, void *)
{
	switch (name)
	{
		case CHIAKI_THREAD_NAME_TAKION:
		case CHIAKI_THREAD_NAME_TAKION_SEND:
		case CHIAKI_THREAD_NAME_FEEDBACK:
		case CHIAKI_THREAD_NAME_CTRL:
		case CHIAKI_THREAD_NAME_CONGESTION:
			setCurrentThreadGamePriority();
			break;
		default:
			break;
	}
}

void installWindowsStreamThreadPriority()
{
	chiaki_thread_set_affinity_cb(streamThreadPriority, nullptr);
}
