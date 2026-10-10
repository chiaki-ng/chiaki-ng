// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import com.metallic.chiaki.lib.DualSenseIntensity

/** Console-requested rumble/trigger intensity, mapped as in the desktop client. */
class DualSenseIntensityState
{
	private var rumble = DualSenseIntensity.STRONG
	private var trigger = DualSenseIntensity.STRONG

	val rumbleOn get() = rumble != DualSenseIntensity.OFF
	val triggersOn get() = trigger != DualSenseIntensity.OFF

	fun setRumble(intensity: DualSenseIntensity) { rumble = intensity }
	fun setTrigger(intensity: DualSenseIntensity) { trigger = intensity }

	/** DualSense output report "power reduction" byte: trigger nibble | rumble nibble */
	val byte: UByte get()
	{
		val triggerNibble = when(trigger)
		{
			DualSenseIntensity.OFF -> 0xF0
			DualSenseIntensity.STRONG -> 0x00
			DualSenseIntensity.MEDIUM -> 0x60
			DualSenseIntensity.WEAK -> 0x90
		}
		val rumbleNibble = when(rumble)
		{
			DualSenseIntensity.OFF -> 0x0F
			DualSenseIntensity.STRONG -> 0x00
			DualSenseIntensity.MEDIUM -> 0x02
			DualSenseIntensity.WEAK -> 0x03
		}
		return (triggerNibble or rumbleNibble).toUByte()
	}

	/** Software scaling for outputs that cannot apply the power reduction byte */
	fun scale(value: UByte): UByte = when(rumble)
	{
		DualSenseIntensity.OFF -> 0U
		DualSenseIntensity.STRONG -> value
		DualSenseIntensity.MEDIUM -> (value.toInt() * 50 / 100).toUByte()
		DualSenseIntensity.WEAK -> (value.toInt() * 33 / 100).toUByte()
	}
}
