// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import com.metallic.chiaki.lib.DualSenseIntensity
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FakeOutput(val deviceId: Int?, override val appliesIntensity: Boolean = false): ControllerOutput
{
	val calls = mutableListOf<String>()
	var closed = false
	override fun rumble(left: UByte, right: UByte) { calls.add("rumble $left $right") }
	override fun led(r: UByte, g: UByte, b: UByte) { calls.add("led $r $g $b") }
	override fun playerIndex(index: Int) { calls.add("player $index") }
	override fun triggerEffects(typeLeft: UByte, left: ByteArray, typeRight: UByte, right: ByteArray) { calls.add("triggers $typeLeft $typeRight") }
	override fun intensity(value: UByte) { calls.add("intensity $value") }
	override fun close() { closed = true }
}

class OutputRouterTest
{
	private val created = mutableListOf<FakeOutput>()
	private fun router(rumbleEnabled: Boolean = true, appliesIntensity: Boolean = false) =
		OutputRouter(rumbleEnabled) { id -> FakeOutput(id, appliesIntensity).also { created.add(it) } }

	@Test
	fun createsOutputForActiveController()
	{
		val r = router()
		r.setActiveController(7)
		r.onRumble(10U, 20U)
		assertEquals(7, created.single().deviceId)
		assertEquals(listOf("intensity 0", "rumble 10 20"), created.single().calls)
	}

	@Test
	fun switchController_closesOldReplaysState()
	{
		val r = router()
		r.setActiveController(1)
		r.onLedColor(1U, 2U, 3U)
		r.onPlayerIndex(0)
		r.onTriggerEffects(0x21U, ByteArray(10), 0x26U, ByteArray(10))
		r.setActiveController(2)
		assertTrue(created[0].closed)
		assertEquals(2, created[1].deviceId)
		assertEquals(listOf("led 1 2 3", "player 0", "intensity 0", "triggers 33 38"), created[1].calls)
	}

	@Test
	fun sameControllerAgain_keepsOutput()
	{
		val r = router()
		r.setActiveController(1)
		r.setActiveController(1)
		assertEquals(1, created.size)
	}

	@Test
	fun activeControllerGone_closesOldAndUsesNull()
	{
		val r = router()
		r.setActiveController(1)
		r.setActiveController(null)
		assertTrue(created[0].closed)
		assertEquals(null, created[1].deviceId)
	}

	@Test
	fun rumbleDisabled_dropsRumbleKeepsLeds()
	{
		val r = router(rumbleEnabled = false)
		r.setActiveController(1)
		r.onRumble(10U, 20U)
		r.onHapticRumble(5U, 5U)
		r.onLedColor(1U, 2U, 3U)
		assertEquals(listOf("intensity 0", "led 1 2 3"), created.single().calls)
	}

	@Test
	fun rumbleScaledByIntensityUnlessOutputAppliesIt()
	{
		val soft = router()
		soft.setActiveController(1)
		soft.onHapticIntensity(DualSenseIntensity.MEDIUM)
		soft.onRumble(200U, 100U)
		assertEquals("rumble 100 50", created.last().calls.last())

		val raw = router(appliesIntensity = true)
		raw.setActiveController(1)
		raw.onHapticIntensity(DualSenseIntensity.MEDIUM)
		raw.onRumble(200U, 100U)
		assertEquals(listOf("intensity 0", "intensity 2", "rumble 200 100"), created.last().calls)
	}

	@Test
	fun hapticRumbleScaledByIntensityUnlessOutputAppliesIt()
	{
		val soft = router()
		soft.setActiveController(1)
		soft.onHapticIntensity(DualSenseIntensity.WEAK)
		soft.onHapticRumble(200U, 100U)
		assertEquals("rumble 66 33", created.last().calls.last())

		val raw = router(appliesIntensity = true)
		raw.setActiveController(1)
		raw.onHapticIntensity(DualSenseIntensity.WEAK)
		raw.onHapticRumble(200U, 100U)
		assertEquals("rumble 200 100", created.last().calls.last())
	}

	@Test
	fun intensityOff_blocksRumbleAndTriggers()
	{
		val r = router()
		r.setActiveController(1)
		r.onHapticIntensity(DualSenseIntensity.OFF)
		r.onTriggerIntensity(DualSenseIntensity.OFF)
		r.onRumble(200U, 100U)
		r.onHapticRumble(50U, 50U)
		r.onTriggerEffects(0x21U, ByteArray(10), 0x21U, ByteArray(10))
		assertEquals(listOf("intensity 0", "intensity 15", "intensity 255"), created.single().calls)
	}

	@Test
	fun rebuild_recreatesForSameDeviceAndReplays()
	{
		val r = router()
		r.setActiveController(3)
		r.onLedColor(9U, 9U, 9U)
		r.rebuild()
		assertTrue(created[0].closed)
		assertEquals(3, created[1].deviceId)
		assertEquals(listOf("led 9 9 9", "intensity 0"), created[1].calls)
	}

	@Test
	fun eventsAfterClose_ignored()
	{
		val r = router()
		r.setActiveController(1)
		r.close()
		r.onRumble(10U, 10U)
		r.onLedColor(1U, 1U, 1U)
		r.setActiveController(2)
		r.rebuild()
		assertTrue(created.single().closed)
		assertEquals(listOf("intensity 0"), created.single().calls)
	}

	@Test
	fun eventBeforeAnyController_createsNullOutput()
	{
		val r = router()
		r.onRumble(10U, 10U)
		assertEquals(null, created.single().deviceId)
	}
}
