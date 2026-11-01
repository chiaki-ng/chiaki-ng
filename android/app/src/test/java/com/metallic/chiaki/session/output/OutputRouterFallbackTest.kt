// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class OutputRouterFallbackTest
{
	@Test
	fun rawFailure_rebuildSwitchesToPublicOutputAndReplays()
	{
		val transport = FakeTransport(succeed = false)
		val rawUnavailable = mutableSetOf<Int>()
		val created = mutableListOf<ControllerOutput>()
		lateinit var router: OutputRouter
		router = OutputRouter(rumbleEnabled = true) { id ->
			val out = if(id != null && id !in rawUnavailable)
				DualSenseBtOutput(transport, { rawUnavailable.add(id); router.rebuild() }, coalesce = false)
			else
				FakeOutput(id)
			out.also { created.add(it) }
		}
		router.setActiveController(4)
		router.onLedColor(1U, 2U, 3U)
		repeat(5) { router.onRumble(10U, 10U) }

		assertTrue(created[0] is DualSenseBtOutput)
		assertTrue(transport.closed)
		val fallback = created.last() as FakeOutput
		assertEquals(4, fallback.deviceId)
		assertEquals(listOf("led 1 2 3", "intensity 0"), fallback.calls.take(2))
	}
}
