// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.settings

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.res.Resources
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.text.InputType
import androidx.lifecycle.Observer
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.preference.*
import com.metallic.chiaki.R
import com.metallic.chiaki.common.Preferences
import com.metallic.chiaki.common.exportAndShareAllSettings
import com.metallic.chiaki.common.ext.viewModelFactory
import com.metallic.chiaki.common.getDatabase
import com.metallic.chiaki.common.importSettingsFromUri

class DataStore(val preferences: Preferences): PreferenceDataStore()
{
	override fun getBoolean(key: String?, defValue: Boolean) = when(key)
	{
		preferences.logVerboseKey -> preferences.logVerbose
		preferences.swapCrossMoonKey -> preferences.swapCrossMoon
		preferences.rumbleEnabledKey -> preferences.rumbleEnabled
		preferences.motionEnabledKey -> preferences.motionEnabled
		preferences.buttonHapticEnabledKey -> preferences.buttonHapticEnabled
		preferences.menuButtonEnabledKey -> preferences.menuButtonEnabled
		preferences.secondScreenControlsEnabledKey -> preferences.secondScreenControlsEnabled
		preferences.secondScreenMenuButtonEnabledKey -> preferences.secondScreenMenuButtonEnabled
		else -> defValue
	}

	override fun putBoolean(key: String?, value: Boolean)
	{
		when(key)
		{
			preferences.logVerboseKey -> preferences.logVerbose = value
			preferences.swapCrossMoonKey -> preferences.swapCrossMoon = value
			preferences.rumbleEnabledKey -> preferences.rumbleEnabled = value
			preferences.motionEnabledKey -> preferences.motionEnabled = value
			preferences.buttonHapticEnabledKey -> preferences.buttonHapticEnabled = value
			preferences.menuButtonEnabledKey -> preferences.menuButtonEnabled = value
			preferences.secondScreenControlsEnabledKey -> preferences.secondScreenControlsEnabled = value
			preferences.secondScreenMenuButtonEnabledKey -> preferences.secondScreenMenuButtonEnabled = value
		}
	}

	override fun getString(key: String, defValue: String?) = when(key)
	{
		preferences.resolutionKey -> preferences.resolution.value
		preferences.fpsKey -> preferences.fps.value
		preferences.bitrateKey -> preferences.bitrate?.toString() ?: ""
		preferences.codecKey -> preferences.codec.value
		preferences.secondScreenLayoutKey -> preferences.secondScreenLayout.value
		else -> defValue
	}

	override fun putString(key: String, value: String?)
	{
		when(key)
		{
			preferences.resolutionKey ->
			{
				val resolution = Preferences.Resolution.values().firstOrNull { it.value == value } ?: return
				preferences.resolution = resolution
			}
			preferences.fpsKey ->
			{
				val fps = Preferences.FPS.values().firstOrNull { it.value == value } ?: return
				preferences.fps = fps
			}
			preferences.bitrateKey -> preferences.bitrate = value?.toIntOrNull()
			preferences.codecKey ->
			{
				val codec = Preferences.Codec.values().firstOrNull { it.value == value } ?: return
				preferences.codec = codec
			}
			preferences.secondScreenLayoutKey ->
			{
				val layout = Preferences.SecondScreenLayout.values().firstOrNull { it.value == value } ?: return
				preferences.secondScreenLayout = layout
			}
		}
	}

	override fun getInt(key: String?, defValue: Int) = when(key)
	{
		preferences.menuButtonOpacityKey -> preferences.menuButtonOpacity
		preferences.secondScreenOpacityKey -> preferences.secondScreenOpacity
		else -> defValue
	}

	override fun putInt(key: String?, value: Int)
	{
		when(key)
		{
			preferences.menuButtonOpacityKey -> preferences.menuButtonOpacity = value
			preferences.secondScreenOpacityKey -> preferences.secondScreenOpacity = value
		}
	}
}

class SettingsFragment: PreferenceFragmentCompat(), TitleFragment
{
	companion object
	{
		private const val PICK_SETTINGS_JSON_REQUEST = 1
	}

	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?)
	{
		val context = context ?: return

		val viewModel = ViewModelProvider(this, viewModelFactory { SettingsViewModel(getDatabase(context), Preferences(context)) })
			.get(SettingsViewModel::class.java)

		val preferences = viewModel.preferences
		preferenceManager.preferenceDataStore = DataStore(preferences)
		setPreferencesFromResource(R.xml.preferences, rootKey)

		preferenceScreen.findPreference<ListPreference>(getString(R.string.preferences_resolution_key))?.let {
			it.entryValues = Preferences.resolutionAll.map { res -> res.value }.toTypedArray()
			it.entries = Preferences.resolutionAll.map { res -> getString(res.title) }.toTypedArray()
		}

		preferenceScreen.findPreference<ListPreference>(getString(R.string.preferences_fps_key))?.let {
			it.entryValues = Preferences.fpsAll.map { fps -> fps.value }.toTypedArray()
			it.entries = Preferences.fpsAll.map { fps -> getString(fps.title) }.toTypedArray()
		}

		val bitratePreference = preferenceScreen.findPreference<EditTextPreference>(getString(R.string.preferences_bitrate_key))
		val bitrateSummaryProvider = Preference.SummaryProvider<EditTextPreference> {
			preferences.bitrate?.toString() ?: getString(R.string.preferences_bitrate_auto, preferences.bitrateAuto)
		}
		bitratePreference?.let {
			it.summaryProvider = bitrateSummaryProvider
			it.setOnBindEditTextListener { editText ->
				editText.hint = getString(R.string.preferences_bitrate_auto, preferences.bitrateAuto)
				editText.inputType = InputType.TYPE_CLASS_NUMBER
				editText.setText(preferences.bitrate?.toString() ?: "")
			}
		}
		viewModel.bitrateAuto.observe(this, Observer {
			bitratePreference?.summaryProvider = bitrateSummaryProvider
		})

		preferenceScreen.findPreference<ListPreference>(getString(R.string.preferences_codec_key))?.let {
			it.entryValues = Preferences.codecAll.map { codec -> codec.value }.toTypedArray()
			it.entries = Preferences.codecAll.map { codec -> getString(codec.title) }.toTypedArray()
		}

		val registeredHostsPreference = preferenceScreen.findPreference<Preference>("registered_hosts")
		viewModel.registeredHostsCount.observe(this, Observer {
			registeredHostsPreference?.summary = getString(R.string.preferences_registered_hosts_summary, it)
		})

		preferenceScreen.findPreference<Preference>(getString(R.string.preferences_export_settings_key))?.setOnPreferenceClickListener { exportSettings(); true }
		preferenceScreen.findPreference<Preference>(getString(R.string.preferences_import_settings_key))?.setOnPreferenceClickListener { importSettings(); true }

		val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
		val hasSecondDisplay = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).isNotEmpty()
		preferenceScreen.findPreference<PreferenceCategory>("category_second_screen")?.isVisible = hasSecondDisplay

		preferenceScreen.findPreference<ListPreference>(getString(R.string.preferences_second_screen_layout_key))?.let {
			it.entryValues = Preferences.secondScreenLayoutAll.map { layout -> layout.value }.toTypedArray()
			it.entries = Preferences.secondScreenLayoutAll.map { layout -> getString(layout.title) }.toTypedArray()
		}
	}

	override fun getTitle(resources: Resources): String = resources.getString(R.string.title_settings)

	private fun exportSettings()
	{
		val activity = activity ?: return
		exportAndShareAllSettings(activity, lifecycleScope)
	}

	private fun importSettings()
	{
		val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
			addCategory(Intent.CATEGORY_OPENABLE)
			type = "application/json"
		}
		startActivityForResult(intent, PICK_SETTINGS_JSON_REQUEST)
	}

	override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?)
	{
		if(requestCode == PICK_SETTINGS_JSON_REQUEST && resultCode == Activity.RESULT_OK)
		{
			val activity = activity ?: return
			data?.data?.also {
				importSettingsFromUri(activity, it, lifecycleScope)
			}
		}
	}
}
