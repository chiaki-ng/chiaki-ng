// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session

import com.metallic.chiaki.common.Preferences.DualSenseMode

object DualSense
{
	const val VENDOR_SONY = 0x054c
	private val PRODUCTS = setOf(
		0x0ce6, // DualSense
		0x0df2  // DualSense Edge
	)

	fun isDualSense(info: InputDeviceInfo) = info.vendorId == VENDOR_SONY && info.productId in PRODUCTS

	fun shouldEnable(mode: DualSenseMode, devices: List<InputDeviceInfo>) = when(mode)
	{
		DualSenseMode.ON -> true
		DualSenseMode.OFF -> false
		DualSenseMode.AUTO -> devices.any { it.isGamepad && isDualSense(it) }
	}
}
