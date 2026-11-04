// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.common

import android.content.Context
import java.io.File
import java.io.FilenameFilter
import java.text.SimpleDateFormat
import java.util.*
import java.util.regex.Pattern

// must match the provider authority in AndroidManifest.xml
val Context.fileProviderAuthority get() = "$packageName.fileprovider"
private const val baseDirName = "session_logs" // must be in sync with filepaths.xml
private val dateFormat = SimpleDateFormat("yyyy-MM-dd_HH-mm-ss-SSS", Locale.US)
private const val filePrefix = "chiaki_session_"
private const val filePostfix = ".log"
private val fileRegex = Regex("$filePrefix(.*)$filePostfix")
private const val keepLogFilesCount = 5

class LogFile private constructor(val logManager: LogManager, val filename: String)
{
	val date = fileRegex.matchEntire(filename)?.groupValues?.get(1)?.let {
		dateFormat.parse(it)
	} ?: throw IllegalArgumentException()

	val file get() = File(logManager.baseDir, filename)

	companion object
	{
		fun fromFilename(logManager: LogManager, filename: String) = try { LogFile(logManager, filename) } catch(e: IllegalArgumentException) { null }
	}
}

class LogManager(context: Context)
{
	val baseDir = File(context.filesDir, baseDirName).also {
		it.mkdirs()
	}

	val files: List<LogFile> get() =
		(baseDir.list { _, s -> s.matches(fileRegex) }?.toList() ?: listOf()).mapNotNull {
			LogFile.fromFilename(this, it)
		}.sortedByDescending {
			it.date
		}

	fun createNewFile(): LogFile
	{
		val currentFiles = files
		if(currentFiles.size > keepLogFilesCount)
		{
			currentFiles.subList(keepLogFilesCount, currentFiles.size).forEach {
				it.file.delete()
			}
		}

		val date = Date()
		val filename = "$filePrefix${dateFormat.format(date)}$filePostfix"
		return LogFile.fromFilename(this, filename)!!
	}
}