// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.common.Preferences.MotionSource

enum class MotionInput
{
	CONTROLLER,
	PHONE,
	NONE
}

fun resolveMotionInput(source: MotionSource, controllerMotionAvailable: Boolean) = when(source)
{
	MotionSource.AUTO -> if(controllerMotionAvailable) MotionInput.CONTROLLER else MotionInput.PHONE
	MotionSource.CONTROLLER -> if(controllerMotionAvailable) MotionInput.CONTROLLER else MotionInput.NONE
	MotionSource.PHONE -> MotionInput.PHONE
	MotionSource.OFF -> MotionInput.NONE
}
