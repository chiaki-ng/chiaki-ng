package com.metallic.chiaki.lib

import android.os.Parcelable
import android.util.Log
import android.view.Surface
import kotlinx.parcelize.Parcelize
import java.lang.Exception
import java.net.InetSocketAddress
import kotlin.math.abs

enum class Target(val value: Int)
{
	PS4_UNKNOWN(0),
	PS4_8(800),
	PS4_9(900),
	PS4_10(1000),
	PS5_UNKNOWN(1000000),
	PS5_1(1000100);

	companion object
	{
		@JvmStatic
		fun fromValue(value: Int) = values().firstOrNull { it.value == value } ?: PS4_10
	}

	val isPS5 get() = value >= PS5_UNKNOWN.value
}

enum class VideoResolutionPreset(val value: Int)
{
	RES_360P(1),
	RES_540P(2),
	RES_720P(3),
	RES_1080P(4)
}

enum class VideoFPSPreset(val value: Int)
{
	FPS_30(30),
	FPS_60(60)
}

enum class Codec(val value: Int)
{
	CODEC_H264(0),
	CODEC_H265(1),
	CODEC_H265_HDR(2)
}

@Parcelize
data class ConnectVideoProfile(
	val width: Int,
	val height: Int,
	val maxFPS: Int,
	val bitrate: Int,
	val codec: Codec
): Parcelable
{
	companion object
	{
		fun preset(resolutionPreset: VideoResolutionPreset, fpsPreset: VideoFPSPreset, codec: Codec)
				= ChiakiNative.videoProfilePreset(resolutionPreset.value, fpsPreset.value, codec)
	}
}

@Parcelize
data class ConnectInfo(
	val ps5: Boolean,
	val host: String,
	val registKey: ByteArray,
	val morning: ByteArray,
	val videoProfile: ConnectVideoProfile,
	val enableDualSense: Boolean = false
): Parcelable

private class ChiakiNative
{
	data class CreateResult(var errorCode: Int, var ptr: Long)
	companion object
	{
		init
		{
			System.loadLibrary("chiaki-jni")
		}
		@JvmStatic external fun errorCodeToString(value: Int): String
		@JvmStatic external fun quitReasonToString(value: Int): String
		@JvmStatic external fun quitReasonIsError(value: Int): Boolean
		@JvmStatic external fun videoProfilePreset(resolutionPreset: Int, fpsPreset: Int, codec: Codec): ConnectVideoProfile
		@JvmStatic external fun sessionCreate(result: CreateResult, connectInfo: ConnectInfo, logFile: String?, logVerbose: Boolean, javaSession: Session)
		@JvmStatic external fun sessionFree(ptr: Long)
		@JvmStatic external fun sessionStart(ptr: Long): Int
		@JvmStatic external fun sessionStop(ptr: Long): Int
		@JvmStatic external fun sessionJoin(ptr: Long): Int
		@JvmStatic external fun sessionSetSurface(ptr: Long, surface: Surface?)
		@JvmStatic external fun sessionSetControllerState(ptr: Long, controllerState: ControllerState)
		@JvmStatic external fun sessionSetLoginPin(ptr: Long, pin: String)
		@JvmStatic external fun motionTrackerCreate(): Long
		@JvmStatic external fun motionTrackerFree(ptr: Long)
		@JvmStatic external fun motionTrackerUpdateAccel(ptr: Long, x: Float, y: Float, z: Float, timestampUs: Int, out: FloatArray)
		@JvmStatic external fun motionTrackerUpdateGyro(ptr: Long, x: Float, y: Float, z: Float, timestampUs: Int, out: FloatArray)
		@JvmStatic external fun motionTrackerReset(ptr: Long, out: FloatArray)
		@JvmStatic external fun discoveryServiceCreate(result: CreateResult, options: DiscoveryServiceOptions, javaService: DiscoveryService)
		@JvmStatic external fun discoveryServiceFree(ptr: Long)
		@JvmStatic external fun discoveryServiceWakeup(ptr: Long, host: String, userCredential: Long, ps5: Boolean)
		@JvmStatic external fun registStart(result: CreateResult, registInfo: RegistInfo, javaLog: ChiakiLog, javaRegist: Regist)
		@JvmStatic external fun registStop(ptr: Long)
		@JvmStatic external fun registFree(ptr: Long)
	}
}

class ErrorCode(val value: Int)
{
	override fun toString() = ChiakiNative.errorCodeToString(value)
	var isSuccess = value == 0
}

class ChiakiLog(val levelMask: Int, val callback: (level: Int, text: String) -> Unit)
{
	companion object
	{
		fun formatLog(level: Int, text: String) =
			"[${when(level)
				{
					Level.DEBUG.value -> "D"
					Level.VERBOSE.value -> "V"
					Level.INFO.value -> "I"
					Level.WARNING.value -> "W"
					Level.ERROR.value -> "E"
					else -> "?"
				}
			}] $text"
	}

	enum class Level(val value: Int)
	{
		DEBUG(1 shl 4),
		VERBOSE(1 shl 3),
		INFO(1 shl 2),
		WARNING(1 shl 1),
		ERROR(1 shl 0),
		ALL(0.inv())
	}

	private fun log(level: Int, text: String)
	{
		callback(level, text)
	}

	fun d(text: String) = log(Level.DEBUG.value, text)
	fun v(text: String) = log(Level.VERBOSE.value, text)
	fun i(text: String) = log(Level.INFO.value, text)
	fun w(text: String) = log(Level.WARNING.value, text)
	fun e(text: String) = log(Level.ERROR.value, text)
}

private fun maxAbs(a: Short, b: Short) = if(abs(a.toInt()) > abs(b.toInt())) a else b

private val CONTROLLER_TOUCHES_MAX = 2 // must be the same as CHIAKI_CONTROLLER_TOUCHES_MAX

data class ControllerTouch(
	var x: UShort = 0U,
	var y: UShort = 0U,
	var id: Byte = -1 // -1 = up
)

data class ControllerState constructor(
	var buttons: UInt = 0U,
	var l2State: UByte = 0U,
	var r2State: UByte = 0U,
	var leftX: Short = 0,
	var leftY: Short = 0,
	var rightX: Short = 0,
	var rightY: Short = 0,
	private var touchIdNext: UByte = 0U,
	var touches: Array<ControllerTouch> = arrayOf(ControllerTouch(), ControllerTouch()),
	var gyroX: Float = 0.0f,
	var gyroY: Float = 0.0f,
	var gyroZ: Float = 0.0f,
	var accelX: Float = 0.0f,
	var accelY: Float = 1.0f,
	var accelZ: Float = 0.0f,
	var orientX: Float = 0.0f,
	var orientY: Float = 0.0f,
	var orientZ: Float = 0.0f,
	var orientW: Float = 1.0f
){
	companion object
	{
		val BUTTON_CROSS 		= (1 shl 0).toUInt()
		val BUTTON_MOON 		= (1 shl 1).toUInt()
		val BUTTON_BOX 			= (1 shl 2).toUInt()
		val BUTTON_PYRAMID 		= (1 shl 3).toUInt()
		val BUTTON_DPAD_LEFT 	= (1 shl 4).toUInt()
		val BUTTON_DPAD_RIGHT	= (1 shl 5).toUInt()
		val BUTTON_DPAD_UP 		= (1 shl 6).toUInt()
		val BUTTON_DPAD_DOWN 	= (1 shl 7).toUInt()
		val BUTTON_L1 			= (1 shl 8).toUInt()
		val BUTTON_R1 			= (1 shl 9).toUInt()
		val BUTTON_L3			= (1 shl 10).toUInt()
		val BUTTON_R3			= (1 shl 11).toUInt()
		val BUTTON_OPTIONS		= (1 shl 12).toUInt()
		val BUTTON_SHARE 		= (1 shl 13).toUInt()
		val BUTTON_TOUCHPAD		= (1 shl 14).toUInt()
		val BUTTON_PS			= (1 shl 15).toUInt()
		val TOUCHPAD_WIDTH: UShort = 1920U
		val TOUCHPAD_HEIGHT: UShort = 942U
	}

	infix fun or(o: ControllerState) = ControllerState(
		buttons = buttons or o.buttons,
		l2State = maxOf(l2State, o.l2State),
		r2State = maxOf(r2State, o.r2State),
		leftX = maxAbs(leftX, o.leftX),
		leftY = maxAbs(leftY, o.leftY),
		rightX = maxAbs(rightX, o.rightX),
		rightY = maxAbs(rightY, o.rightY),
		touches = touches.zip(o.touches) { a, b -> if(a.id >= 0) a else b }.toTypedArray(),
		gyroX = gyroX,
		gyroY = gyroY,
		gyroZ = gyroZ,
		accelX = accelX,
		accelY = accelY,
		accelZ = accelZ,
		orientX = orientX,
		orientY = orientY,
		orientZ = orientZ,
		orientW = orientW
	)

	override fun equals(other: Any?): Boolean
	{
		if(this === other) return true
		if(javaClass != other?.javaClass) return false

		other as ControllerState

		if(buttons != other.buttons) return false
		if(l2State != other.l2State) return false
		if(r2State != other.r2State) return false
		if(leftX != other.leftX) return false
		if(leftY != other.leftY) return false
		if(rightX != other.rightX) return false
		if(rightY != other.rightY) return false
		if(touchIdNext != other.touchIdNext) return false
		if(!touches.contentEquals(other.touches)) return false
		if(gyroX != other.gyroX) return false
		if(gyroY != other.gyroY) return false
		if(gyroZ != other.gyroZ) return false
		if(accelX != other.accelX) return false
		if(accelY != other.accelY) return false
		if(accelZ != other.accelZ) return false
		if(orientX != other.orientX) return false
		if(orientY != other.orientY) return false
		if(orientZ != other.orientZ) return false
		if(orientW != other.orientW) return false

		return true
	}

	override fun hashCode(): Int
	{
		var result = buttons.hashCode()
		result = 31 * result + l2State.hashCode()
		result = 31 * result + r2State.hashCode()
		result = 31 * result + leftX
		result = 31 * result + leftY
		result = 31 * result + rightX
		result = 31 * result + rightY
		result = 31 * result + touchIdNext.hashCode()
		result = 31 * result + touches.contentHashCode()
		result = 31 * result + gyroX.hashCode()
		result = 31 * result + gyroY.hashCode()
		result = 31 * result + gyroZ.hashCode()
		result = 31 * result + accelX.hashCode()
		result = 31 * result + accelY.hashCode()
		result = 31 * result + accelZ.hashCode()
		result = 31 * result + orientX.hashCode()
		result = 31 * result + orientY.hashCode()
		result = 31 * result + orientZ.hashCode()
		result = 31 * result + orientW.hashCode()
		return result
	}

	fun startTouch(x: UShort, y: UShort): UByte? =
		touches
			.find { it.id < 0 }
			?.also {
				it.id = touchIdNext.toByte()
				it.x = x
				it.y = y
				touchIdNext = ((touchIdNext + 1U) and 0x7fU).toUByte()
			}?.id?.toUByte()

	fun stopTouch(id: UByte)
	{
		touches.find {
			it.id >= 0 && it.id == id.toByte()
		}?.let {
			it.id = -1
		}
	}

	fun setTouchPos(id: UByte, x: UShort, y: UShort): Boolean
		= touches.find {
			it.id >= 0 && it.id == id.toByte()
		}?.let {
			val r = it.x != x || it.y != y
			it.x = x
			it.y = y
			r
		} ?: false
}

class QuitReason(val value: Int)
{
	override fun toString() = ChiakiNative.quitReasonToString(value)

	val isError = ChiakiNative.quitReasonIsError(value)
}

sealed class Event
object ConnectedEvent: Event()
data class LoginPinRequestEvent(val pinIncorrect: Boolean): Event()
data class QuitEvent(val reason: QuitReason, val reasonString: String?): Event()
data class RumbleEvent(val left: UByte, val right: UByte): Event()
object MotionResetEvent: Event()

enum class DualSenseIntensity(val value: Int)
{
	OFF(0),
	STRONG(1),
	MEDIUM(2),
	WEAK(3);

	companion object
	{
		fun fromValue(value: Int) = values().firstOrNull { it.value == value } ?: STRONG
	}
}

class TriggerEffectsEvent(val typeLeft: UByte, val left: ByteArray, val typeRight: UByte, val right: ByteArray): Event()
data class LedColorEvent(val r: UByte, val g: UByte, val b: UByte): Event()
data class PlayerIndexEvent(val index: Int): Event()
data class HapticIntensityEvent(val intensity: DualSenseIntensity): Event()
data class TriggerIntensityEvent(val intensity: DualSenseIntensity): Event()
data class HapticStrengthEvent(val left: Int, val right: Int): Event()

class CreateError(val errorCode: ErrorCode): Exception("Failed to create a native object: $errorCode")

class Session(connectInfo: ConnectInfo, logFile: String?, logVerbose: Boolean)
{
	interface EventCallback
	{
		fun sessionEvent(event: Event)
	}

	private var nativePtr: Long
	var eventCallback: ((event: Event) -> Unit)? = null

	init
	{
		val result = ChiakiNative.CreateResult(0, 0)
		ChiakiNative.sessionCreate(result, connectInfo, logFile, logVerbose, this)
		val errorCode = ErrorCode(result.errorCode)
		if(!errorCode.isSuccess)
			throw CreateError(errorCode)
		nativePtr = result.ptr
	}

	fun start() = ErrorCode(ChiakiNative.sessionStart(nativePtr))
	fun stop() = ErrorCode(ChiakiNative.sessionStop(nativePtr))

	fun dispose()
	{
		if(nativePtr == 0L)
			return
		ChiakiNative.sessionJoin(nativePtr)
		ChiakiNative.sessionFree(nativePtr)
		nativePtr = 0L
	}

	private fun event(event: Event)
	{
		eventCallback?.let { it(event) }
	}

	private fun eventConnected()
	{
		event(ConnectedEvent)
	}

	private fun eventLoginPinRequest(pinIncorrect: Boolean)
	{
		event(LoginPinRequestEvent(pinIncorrect))
	}

	private fun eventQuit(reasonValue: Int, reasonString: String?)
	{
		event(QuitEvent(QuitReason(reasonValue), reasonString))
	}

	private fun eventRumble(left: Int, right: Int)
	{
		event(RumbleEvent(left.toUByte(), right.toUByte()))
	}

	private fun eventMotionReset()
	{
		event(MotionResetEvent)
	}

	private fun eventTriggerEffects(typeLeft: Int, left: ByteArray, typeRight: Int, right: ByteArray)
	{
		event(TriggerEffectsEvent(typeLeft.toUByte(), left, typeRight.toUByte(), right))
	}

	private fun eventLedColor(r: Int, g: Int, b: Int)
	{
		event(LedColorEvent(r.toUByte(), g.toUByte(), b.toUByte()))
	}

	private fun eventPlayerIndex(index: Int)
	{
		event(PlayerIndexEvent(index))
	}

	private fun eventHapticIntensity(intensity: Int)
	{
		event(HapticIntensityEvent(DualSenseIntensity.fromValue(intensity)))
	}

	private fun eventTriggerIntensity(intensity: Int)
	{
		event(TriggerIntensityEvent(DualSenseIntensity.fromValue(intensity)))
	}

	private fun eventHapticStrength(left: Int, right: Int)
	{
		event(HapticStrengthEvent(left, right))
	}

	fun setSurface(surface: Surface?)
	{
		ChiakiNative.sessionSetSurface(nativePtr, surface)
	}

	fun setControllerState(controllerState: ControllerState)
	{
		ChiakiNative.sessionSetControllerState(nativePtr, controllerState)
	}

	fun setLoginPin(pin: String)
	{
		ChiakiNative.sessionSetLoginPin(nativePtr, pin)
	}
}

/**
 * Orientation tracking for controller motion, using the same
 * Madgwick filter and accel-zero handling as the desktop client.
 * Accel in g, gyro in rad/s, timestamps in microseconds (wrapping).
 */
class MotionTracker
{
	private var nativePtr = ChiakiNative.motionTrackerCreate()
	companion object
	{
		fun neutral(state: ControllerState)
		{
			state.gyroX = 0.0f
			state.gyroY = 0.0f
			state.gyroZ = 0.0f
			state.accelX = 0.0f
			state.accelY = 1.0f
			state.accelZ = 0.0f
			state.orientX = 0.0f
			state.orientY = 0.0f
			state.orientZ = 0.0f
			state.orientW = 1.0f
		}
	}

	private val values = FloatArray(10).also { it[4] = 1.0f; it[9] = 1.0f }

	fun updateAccel(x: Float, y: Float, z: Float, timestampUs: Int)
	{
		if(nativePtr != 0L)
			ChiakiNative.motionTrackerUpdateAccel(nativePtr, x, y, z, timestampUs, values)
	}

	fun updateGyro(x: Float, y: Float, z: Float, timestampUs: Int)
	{
		if(nativePtr != 0L)
			ChiakiNative.motionTrackerUpdateGyro(nativePtr, x, y, z, timestampUs, values)
	}

	fun reset()
	{
		if(nativePtr != 0L)
			ChiakiNative.motionTrackerReset(nativePtr, values)
	}

	fun applyTo(state: ControllerState)
	{
		state.gyroX = values[0]
		state.gyroY = values[1]
		state.gyroZ = values[2]
		state.accelX = values[3]
		state.accelY = values[4]
		state.accelZ = values[5]
		state.orientX = values[6]
		state.orientY = values[7]
		state.orientZ = values[8]
		state.orientW = values[9]
	}

	fun dispose()
	{
		if(nativePtr == 0L)
			return
		ChiakiNative.motionTrackerFree(nativePtr)
		nativePtr = 0L
	}
}

data class DiscoveryHost(
	val state: State,
	val hostRequestPort: UShort,
	val hostAddr: String?,
	val systemVersion: String?,
	val deviceDiscoveryProtocolVersion: String?,
	val hostName: String?,
	val hostType: String?,
	val hostId: String?,
	val runningAppTitleid: String?,
	val runningAppName: String?)
{
	enum class State
	{
		UNKNOWN,
		READY,
		STANDBY
	}
	
	val isPS5 get() = deviceDiscoveryProtocolVersion == "00030010"
}


data class DiscoveryServiceOptions(
	val hostsMax: ULong,
	val hostDropPings: ULong,
	val pingMs: ULong,
	val sendAddr: InetSocketAddress
)

class DiscoveryService(
	options: DiscoveryServiceOptions,
	val callback: ((hosts: List<DiscoveryHost>) -> Unit)?)
{
	companion object
	{
		fun wakeup(service: DiscoveryService?, host: String, userCredential: ULong, ps5: Boolean) =
			ChiakiNative.discoveryServiceWakeup(service?.nativePtr ?: 0, host, userCredential.toLong(), ps5)
	}

	private var nativePtr: Long

	init
	{
		val result = ChiakiNative.CreateResult(0, 0)
		ChiakiNative.discoveryServiceCreate(result, options, this)
		val errorCode = ErrorCode(result.errorCode)
		if(!errorCode.isSuccess)
			throw CreateError(errorCode)
		nativePtr = result.ptr
	}

	fun dispose()
	{
		if(nativePtr == 0L)
			return
		ChiakiNative.discoveryServiceFree(nativePtr)
		nativePtr = 0L
	}

	private fun hostsUpdated(hosts: Array<DiscoveryHost>)
	{
		val hostsList = hosts.toList()
		Log.i("Chiaki", "got hosts from native: $hostsList")
		callback?.let { it(hostsList) }
	}

}

@Parcelize
data class RegistInfo(
	val target: Target,
	val host: String,
	val broadcast: Boolean,
	val psnOnlineId: String?,
	val psnAccountId: ByteArray?,
	val pin: Int
): Parcelable
{
	companion object
	{
		const val ACCOUNT_ID_SIZE = 8
	}
}

data class RegistHost(
	val target: Target,
	val apSsid: String,
	val apBssid: String,
	val apKey: String,
	val apName: String,
	val serverMac: ByteArray,
	val serverNickname: String,
	val rpRegistKey: ByteArray,
	val rpKeyType: UInt,
	val rpKey: ByteArray
)

sealed class RegistEvent
object RegistEventCanceled: RegistEvent()
object RegistEventFailed: RegistEvent()
class RegistEventSuccess(val host: RegistHost): RegistEvent()

class Regist(
	info: RegistInfo,
	log: ChiakiLog,
	val callback: (RegistEvent) -> Unit
)
{
	private var nativePtr: Long

	init
	{
		val result = ChiakiNative.CreateResult(0, 0)
		ChiakiNative.registStart(result, info, log, this)
		val errorCode = ErrorCode(result.errorCode)
		if(!errorCode.isSuccess)
			throw CreateError(errorCode)
		nativePtr = result.ptr
	}

	fun stop()
	{
		ChiakiNative.registStop(nativePtr)
	}

	fun dispose()
	{
		if(nativePtr == 0L)
			return
		ChiakiNative.registFree(nativePtr)
		nativePtr = 0L
	}

	private fun event(event: RegistEvent)
	{
		callback(event)
	}
}
