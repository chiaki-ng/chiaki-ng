// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

/** Something that can render the console's controller feedback. Unsupported effects are ignored. */
interface ControllerOutput
{
	/** True if the output applies the console's intensity byte itself (raw DualSense reports). */
	val appliesIntensity: Boolean get() = false

	/** left = low-frequency motor, right = high-frequency motor */
	fun rumble(left: UByte, right: UByte)
	fun led(r: UByte, g: UByte, b: UByte) {}
	fun playerIndex(index: Int) {}
	/** left/right are the 10 data bytes following each effect type */
	fun triggerEffects(typeLeft: UByte, left: ByteArray, typeRight: UByte, right: ByteArray) {}
	fun intensity(value: UByte) {}
	/** Stop all feedback and release resources. */
	fun close()
}
