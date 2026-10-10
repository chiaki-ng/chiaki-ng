// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.session.output

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.util.Log
import android.view.InputDevice
import androidx.annotation.RequiresApi
import org.lsposed.hiddenapibypass.HiddenApiBypass
import java.lang.reflect.InvocationTargetException
import java.lang.reflect.Method

/**
 * Access to the hidden BluetoothHidHost profile, whose sendData() writes
 * raw output reports to a paired HID device.
 */
@SuppressLint("MissingPermission")
@RequiresApi(Build.VERSION_CODES.S)
class HidHostProxy(context: Context, private val onReady: () -> Unit)
{
	companion object
	{
		private const val TAG = "HidHostProxy"
		private const val HID_HOST = 4 // BluetoothProfile.HID_HOST (hidden)
	}

	private val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
	@Volatile private var proxy: BluetoothProfile? = null
	@Volatile private var sendData: Method? = null

	val isReady get() = proxy != null && sendData != null

	private val listener = object: BluetoothProfile.ServiceListener {
		override fun onServiceConnected(profile: Int, p: BluetoothProfile)
		{
			sendData = try
			{
				p.javaClass.getMethod("sendData", BluetoothDevice::class.java, String::class.java)
			}
			catch(e: NoSuchMethodException)
			{
				Log.w(TAG, "sendData not reachable: $e")
				null
			}
			proxy = p
			onReady()
		}

		override fun onServiceDisconnected(profile: Int)
		{
			proxy = null
			onReady()
		}
	}

	init
	{
		Log.i(TAG, "hidden api exemptions: ${HiddenApiBypass.addHiddenApiExemptions("L")}")
		if(adapter?.getProfileProxy(context, listener, HID_HOST) != true)
			Log.w(TAG, "HID host profile proxy unavailable")
	}

	fun transportFor(inputDevice: InputDevice): RawReportTransport?
	{
		val p = proxy ?: return null
		val method = sendData ?: return null
		val candidates = runCatching {
			p.connectedDevices.filter { it.name?.contains("DualSense") == true }
		}.getOrElse {
			Log.w(TAG, "cannot list HID devices: $it")
			return null
		}
		val address = bluetoothAddressOf(inputDevice)
		val device = if(address != null)
			candidates.firstOrNull { it.address.equals(address, ignoreCase = true) }
		else
			candidates.firstOrNull().also {
				if(candidates.size > 1)
					Log.w(TAG, "${candidates.size} DualSense pads connected, cannot tell which one is active; using ${it?.address}")
			}
		device ?: return null
		return object: RawReportTransport {
			override fun send(report: ByteArray): Boolean
			{
				val hex = report.joinToString("") { "%02x".format(it) }
				return try
				{
					method.invoke(p, device, hex) as? Boolean ?: false
				}
				catch(e: InvocationTargetException)
				{
					throw (e.cause as? Exception) ?: e
				}
			}

			override fun close() {}
		}
	}

	private fun bluetoothAddressOf(device: InputDevice): String? = runCatching {
		InputDevice::class.java.getMethod("getBluetoothAddress").invoke(device) as? String
	}.getOrNull()

	fun close()
	{
		proxy?.let { adapter?.closeProfileProxy(HID_HOST, it) }
		proxy = null
	}
}
