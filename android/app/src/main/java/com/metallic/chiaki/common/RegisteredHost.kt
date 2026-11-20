// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import androidx.room.*
import androidx.room.ColumnInfo.Companion.BLOB
import com.metallic.chiaki.lib.RegistHost
import com.metallic.chiaki.lib.Target
import kotlinx.coroutines.flow.Flow

@Entity(tableName = "registered_host")
data class RegisteredHost(
	@PrimaryKey(autoGenerate = true) val id: Long = 0,
	@ColumnInfo(name = "target") val target: Target,
	@ColumnInfo(name = "ap_ssid") val apSsid: String?,
	@ColumnInfo(name = "ap_bssid") val apBssid: String?,
	@ColumnInfo(name = "ap_key") val apKey: String?,
	@ColumnInfo(name = "ap_name") val apName: String?,
	@ColumnInfo(name = "server_mac") val serverMac: MacAddress,
	@ColumnInfo(name = "server_nickname") val serverNickname: String?,
	@ColumnInfo(name = "rp_regist_key", typeAffinity = BLOB) val rpRegistKey: ByteArray, // CHIAKI_SESSION_AUTH_SIZE
	@ColumnInfo(name = "rp_key_type") val rpKeyType: Int,
	@ColumnInfo(name = "rp_key", typeAffinity = BLOB) val rpKey: ByteArray // 0x10
)
{
	constructor(registHost: RegistHost) : this(
		target = registHost.target,
		apSsid = registHost.apSsid,
		apBssid = registHost.apBssid,
		apKey = registHost.apKey,
		apName = registHost.apName,
		serverMac = MacAddress(registHost.serverMac),
		serverNickname = registHost.serverNickname,
		rpRegistKey = registHost.rpRegistKey,
		rpKeyType = registHost.rpKeyType.toInt(),
		rpKey = registHost.rpKey
	)

	override fun equals(other: Any?): Boolean
	{
		if(this === other) return true
		if(javaClass != other?.javaClass) return false

		other as RegisteredHost

		if(id != other.id) return false
		if(target != other.target) return false
		if(apSsid != other.apSsid) return false
		if(apBssid != other.apBssid) return false
		if(apKey != other.apKey) return false
		if(apName != other.apName) return false
		if(serverMac != other.serverMac) return false
		if(serverNickname != other.serverNickname) return false
		if(!rpRegistKey.contentEquals(other.rpRegistKey)) return false
		if(rpKeyType != other.rpKeyType) return false
		if(!rpKey.contentEquals(other.rpKey)) return false

		return true
	}

	override fun hashCode(): Int
	{
		var result = id.hashCode()
		result = 31 * result + target.hashCode()
		result = 31 * result + (apSsid?.hashCode() ?: 0)
		result = 31 * result + (apBssid?.hashCode() ?: 0)
		result = 31 * result + (apKey?.hashCode() ?: 0)
		result = 31 * result + (apName?.hashCode() ?: 0)
		result = 31 * result + serverMac.hashCode()
		result = 31 * result + (serverNickname?.hashCode() ?: 0)
		result = 31 * result + rpRegistKey.contentHashCode()
		result = 31 * result + rpKeyType
		result = 31 * result + rpKey.contentHashCode()
		return result
	}
}

@Dao
interface RegisteredHostDao
{
	@Query("SELECT * FROM registered_host")
	fun getAll(): Flow<List<RegisteredHost>>

	@Query("SELECT * FROM registered_host WHERE server_mac == :mac LIMIT 1")
	suspend fun getByMac(mac: MacAddress): RegisteredHost?

	@Query("DELETE FROM registered_host WHERE server_mac == :mac")
	suspend fun deleteByMac(mac: MacAddress)

	@Delete
	suspend fun delete(host: RegisteredHost)

	@Query("SELECT COUNT(*) FROM registered_host")
	fun count(): Flow<Int>

	@Insert
	suspend fun insert(host: RegisteredHost): Long
}