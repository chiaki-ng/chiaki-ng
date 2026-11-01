// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_WINDOWSSTREAMTUNING_H
#define CHIAKI_WINDOWSSTREAMTUNING_H

// Windows scheduling tweaks for streaming (adapted from P5M for Windows,
// https://github.com/beecrepaldi-afk/P5M-Windows, AGPL-3.0).

// While a stream runs: 1 ms timer resolution and no power throttling
// (EcoQoS) for the process, so the network and render threads wake on time
// even when the window is not in front.
void setWindowsStreamActivity(bool active);

// The calling thread joins the MMCSS "Games" task at high priority
// (fallback: above-normal thread priority).
void setCurrentThreadGamePriority();

// The Takion, send, feedback, control and congestion threads of the stream
// get setCurrentThreadGamePriority() as they start.
void installWindowsStreamThreadPriority();

#endif // CHIAKI_WINDOWSSTREAMTUNING_H
