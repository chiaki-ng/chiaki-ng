// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import android.app.Activity
import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.util.Base64
import android.util.Log
import androidx.core.content.FileProvider
import androidx.room.*
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.metallic.chiaki.R
import com.metallic.chiaki.lib.Target
import com.squareup.moshi.*
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okio.Buffer
import okio.buffer
import okio.source
import java.io.File
import java.io.IOException

@JsonClass(generateAdapter = true)
class SerializedRegisteredHost(
	@Json(name = "target") val target: Target,
	@Json(name = "ap_ssid") val apSsid: String?,
	@Json(name = "ap_bssid") val apBssid: String?,
	@Json(name = "ap_key") val apKey: String?,
	@Json(name = "ap_name") val apName: String?,
	@Json(name = "server_mac") val serverMac: MacAddress,
	@Json(name = "server_nickname") val serverNickname: String?,
	@Json(name = "rp_regist_key") val rpRegistKey: ByteArray,
	@Json(name = "rp_key_type") val rpKeyType: Int,
	@Json(name = "rp_key") val rpKey: ByteArray
){
	constructor(registeredHost: RegisteredHost) : this(
		registeredHost.target,
		registeredHost.apSsid,
		registeredHost.apBssid,
		registeredHost.apKey,
		registeredHost.apName,
		registeredHost.serverMac,
		registeredHost.serverNickname,
		registeredHost.rpRegistKey,
		registeredHost.rpKeyType,
		registeredHost.rpKey
	)
}

@JsonClass(generateAdapter = true)
class SerializedManualHost(
	@Json(name = "host") val host: String,
	@Json(name = "server_mac") val serverMac: MacAddress?
)

@JsonClass(generateAdapter = true)
data class SerializedSettings(
	@Json(name = "registered_hosts") val registeredHosts: List<SerializedRegisteredHost>,
	@Json(name = "manual_hosts") val manualHosts: List<SerializedManualHost>
)
{
	companion object
	{
		suspend fun fromDatabase(db: AppDatabase): SerializedSettings
		{
			val registeredHosts = db.registeredHostDao().getAll().first()
			val manualHosts = db.manualHostDao().getAll().first()
			return SerializedSettings(
				registeredHosts.map { SerializedRegisteredHost(it) },
				manualHosts.map { manualHost ->
					SerializedManualHost(
						manualHost.host,
						manualHost.registeredHost?.let { registeredHostId ->
							registeredHosts.firstOrNull { it.id == registeredHostId }
						}?.serverMac
					)
				}
			)
		}
	}
}

private class ByteArrayJsonAdapter
{
	@ToJson fun toJson(byteArray: ByteArray) = Base64.encodeToString(byteArray, Base64.NO_WRAP)
	@FromJson fun fromJson(string: String) = Base64.decode(string, Base64.DEFAULT)
}

private fun moshi() =
	Moshi.Builder()
	.add(MacAddressJsonAdapter())
	.add(ByteArrayJsonAdapter())
	.build()

private fun Moshi.serializedSettingsAdapter() =
	adapter(SerializedSettings::class.java)
	.serializeNulls()

private const val KEY_FORMAT = "format"
private const val FORMAT = "chiaki-settings"
private const val KEY_VERSION = "version"
private const val VERSION = 2
private const val KEY_SETTINGS = "settings"

suspend fun exportAllSettings(db: AppDatabase): String = withContext(Dispatchers.IO) {
	val settings = SerializedSettings.fromDatabase(db)
	val buffer = Buffer()
	val writer = JsonWriter.of(buffer)
	val adapter = moshi().serializedSettingsAdapter()
	writer.indent = "  "
	writer.
		beginObject()
		.name(KEY_FORMAT).value(FORMAT)
		.name(KEY_VERSION).value(VERSION)
	writer.name(KEY_SETTINGS)
	adapter.toJson(writer, settings)
	writer.endObject()
	buffer.readUtf8()
}

fun exportAndShareAllSettings(activity: Activity, scope: CoroutineScope)
{
	val db = getDatabase(activity)
	val dir = File(activity.cacheDir, "export_settings")
	dir.mkdirs()
	val file = File(dir, "chiaki-settings.json")
	scope.launch {
		val json = exportAllSettings(db)
		withContext(Dispatchers.IO) {
			file.writeText(json, Charsets.UTF_8)
		}
		val uri = FileProvider.getUriForFile(activity, fileProviderAuthority, file)
		Intent(Intent.ACTION_SEND).also {
			it.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
			it.type = "application/json"
			it.putExtra(Intent.EXTRA_STREAM, uri)
			it.clipData = ClipData.newRawUri("", uri)
			activity.startActivity(Intent.createChooser(it, activity.getString(R.string.action_share_log)))
		}
	}
}

fun importSettingsFromUri(activity: Activity, uri: Uri, scope: CoroutineScope)
{
	fun loadFail(msg: String)
	{
		MaterialAlertDialogBuilder(activity)
			.setMessage(activity.getString(R.string.alert_message_import_failed, msg))
			.setPositiveButton(R.string.action_import_failed_ack) { _, _ -> }
			.create()
			.show()
	}

	try
	{
		val inputStream = activity.contentResolver.openInputStream(uri) ?: throw IOException()
		val buffer = inputStream.source().buffer()
		val reader = JsonReader.of(buffer)
		val adapter = moshi().serializedSettingsAdapter()

		var format: String? = null
		var version: Int? = null
		var settingsValue: Any? = null

		reader.beginObject()
		while(reader.hasNext())
		{
			when(reader.nextName())
			{
				KEY_FORMAT -> format = reader.nextString()
				KEY_VERSION -> version = reader.nextInt()
				KEY_SETTINGS -> settingsValue = reader.readJsonValue()
			}
		}
		reader.endObject()

		if(format == null || version == null || settingsValue == null)
			throw IOException("Missing format, version or settings from JSON")
		if(format != FORMAT)
			throw IOException("Value of format is invalid")
		if(version != VERSION) // Add migrations here when necessary
			throw IOException("Value of version is invalid")

		val settings = adapter.fromJsonValue(settingsValue) ?: throw JsonDataException("Failed to parse Settings JSON")
		Log.i("SerializedSettings", "would import: $settings")

		MaterialAlertDialogBuilder(activity)
			.setMessage(activity.getString(R.string.alert_message_import,
				settings.registeredHosts.let {
					if(it.isEmpty())
						"-"
					else
						it.joinToString(separator = "") { host -> "\n - ${host.serverNickname ?: "?"} / ${host.serverMac}" }
				},
				settings.manualHosts.let {
					if(it.isEmpty())
						"-"
					else
						it.joinToString(separator = "") { host -> "\n - ${host.host} / ${host.serverMac ?: "unregistered"}" }
				}
			))
			.setTitle(R.string.alert_title_import)
			.setNegativeButton(R.string.action_import_cancel) { _, _ ->  }
			.setPositiveButton(R.string.action_import_import) { _, _ ->
				scope.launch(Dispatchers.IO) {
					getDatabase(activity).importDao().import(settings)
				}
			}
			.create()
			.show()
	}
	catch(e: IOException)
	{
		e.printStackTrace()
		loadFail(e.message ?: "")
	}
	catch(e: JsonDataException)
	{
		e.printStackTrace()
		loadFail(e.message ?: "")
	}
}

@Dao
abstract class ImportDao
{
	@Insert
	abstract fun insertRegisteredHosts(hosts: List<RegisteredHost>)

	@Insert
	abstract fun insertManualHosts(hosts: List<ManualHost>)

	class IdWithMac(val id: Long, val mac: MacAddress)

	@Query("SELECT id, server_mac AS mac FROM registered_host WHERE server_mac IN (:macs)")
	abstract fun registeredHostsByMac(macs: List<MacAddress>): List<IdWithMac>

	@Transaction
	fun import(settings: SerializedSettings)
	{
		insertRegisteredHosts(
			settings.registeredHosts.map {
				RegisteredHost(
					target = it.target,
					apSsid = it.apSsid,
					apBssid = it.apBssid,
					apKey = it.apKey,
					apName = it.apName,
					serverMac = it.serverMac,
					serverNickname = it.serverNickname,
					rpRegistKey = it.rpRegistKey,
					rpKeyType = it.rpKeyType,
					rpKey = it.rpKey
				)
		})

		val macs = settings.manualHosts.mapNotNull { it.serverMac }
		val idMacs =
			if(macs.isNotEmpty())
				registeredHostsByMac(macs)
			else
				listOf()

		insertManualHosts(
			settings.manualHosts.map {
				ManualHost(
					host = it.host,
					registeredHost = idMacs.firstOrNull { regHost -> regHost.mac == it.serverMac }?.id
				)
		})
	}
}
