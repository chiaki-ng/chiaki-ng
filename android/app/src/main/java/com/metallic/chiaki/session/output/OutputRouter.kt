// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import com.metallic.chiaki.lib.DualSenseIntensity

/**
 * Sends the console's controller feedback to one output for the active controller.
 * Called from native event threads and the main thread; all methods are synchronized.
 */
class OutputRouter(
	private val rumbleEnabled: Boolean,
	private val createOutput: (deviceId: Int?) -> ControllerOutput)
{
	private class TriggerEffects(val typeLeft: UByte, val left: ByteArray, val typeRight: UByte, val right: ByteArray)

	private var deviceId: Int? = null
	private var output: ControllerOutput? = null
	private var closed = false

	private var ledColor: Triple<UByte, UByte, UByte>? = null
	private var playerIndex: Int? = null
	private var triggerEffects: TriggerEffects? = null
	private val intensity = DualSenseIntensityState()

	@Synchronized
	fun setActiveController(id: Int?)
	{
		if(closed || (output != null && id == deviceId))
			return
		deviceId = id
		replaceOutput()
	}

	/** Recreate the output for the current controller, e.g. after a backend became (un)available. */
	@Synchronized
	fun rebuild()
	{
		if(closed)
			return
		replaceOutput()
	}

	@Synchronized
	fun onRumble(left: UByte, right: UByte)
	{
		if(!rumbleEnabled || !intensity.rumbleOn)
			return
		val out = current() ?: return
		if(out.appliesIntensity)
			out.rumble(left, right)
		else
			out.rumble(intensity.scale(left), intensity.scale(right))
	}

	@Synchronized
	fun onHapticRumble(left: UByte, right: UByte)
	{
		if(!rumbleEnabled || !intensity.rumbleOn)
			return
		val out = current() ?: return
		if(out.appliesIntensity)
			out.rumble(left, right)
		else
			out.rumble(intensity.scale(left), intensity.scale(right))
	}

	@Synchronized
	fun onLedColor(r: UByte, g: UByte, b: UByte)
	{
		if(closed)
			return
		ledColor = Triple(r, g, b)
		current()?.led(r, g, b)
	}

	@Synchronized
	fun onPlayerIndex(index: Int)
	{
		if(closed)
			return
		playerIndex = index
		current()?.playerIndex(index)
	}

	@Synchronized
	fun onTriggerEffects(typeLeft: UByte, left: ByteArray, typeRight: UByte, right: ByteArray)
	{
		if(closed || !intensity.triggersOn)
			return
		triggerEffects = TriggerEffects(typeLeft, left, typeRight, right)
		current()?.triggerEffects(typeLeft, left, typeRight, right)
	}

	@Synchronized
	fun onHapticIntensity(value: DualSenseIntensity)
	{
		if(closed)
			return
		intensity.setRumble(value)
		current()?.intensity(intensity.byte)
	}

	@Synchronized
	fun onTriggerIntensity(value: DualSenseIntensity)
	{
		if(closed)
			return
		intensity.setTrigger(value)
		current()?.intensity(intensity.byte)
	}

	@Synchronized
	fun close()
	{
		closed = true
		output?.close()
		output = null
	}

	private fun current(): ControllerOutput?
	{
		if(closed)
			return null
		return output ?: replaceOutput()
	}

	private fun replaceOutput(): ControllerOutput
	{
		output?.close()
		val out = createOutput(deviceId)
		output = out
		ledColor?.let { (r, g, b) -> out.led(r, g, b) }
		playerIndex?.let { out.playerIndex(it) }
		out.intensity(intensity.byte)
		if(intensity.triggersOn)
			triggerEffects?.let { out.triggerEffects(it.typeLeft, it.left, it.typeRight, it.right) }
		return out
	}
}
