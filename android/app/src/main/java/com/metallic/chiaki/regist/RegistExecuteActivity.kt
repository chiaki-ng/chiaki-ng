// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.regist

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.View
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.IntentCompat
import androidx.lifecycle.Observer
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.metallic.chiaki.R
import com.metallic.chiaki.common.MacAddress
import com.metallic.chiaki.common.ext.viewModelFactory
import com.metallic.chiaki.common.getDatabase
import com.metallic.chiaki.databinding.ActivityRegistExecuteBinding
import com.metallic.chiaki.lib.RegistInfo
import kotlin.math.max

class RegistExecuteActivity: AppCompatActivity()
{
	companion object
	{
		const val EXTRA_REGIST_INFO = "regist_info"
		const val EXTRA_ASSIGN_MANUAL_HOST_ID = "assign_manual_host_id"

		const val RESULT_FAILED = Activity.RESULT_FIRST_USER
	}

	private lateinit var viewModel: RegistExecuteViewModel
	private lateinit var binding: ActivityRegistExecuteBinding

	override fun onCreate(savedInstanceState: Bundle?)
	{
		super.onCreate(savedInstanceState)
		binding = ActivityRegistExecuteBinding.inflate(layoutInflater)
		setContentView(binding.root)

		viewModel = ViewModelProvider(this, viewModelFactory { RegistExecuteViewModel(getDatabase(this)) })
			.get(RegistExecuteViewModel::class.java)

		binding.logTextView.setHorizontallyScrolling(true)
		binding.logTextView.movementMethod = ScrollingMovementMethod()
		viewModel.logText.observe(this, Observer {
			val textLayout = binding.logTextView.layout ?: return@Observer
			val lineCount = textLayout.lineCount
			if(lineCount < 1)
				return@Observer
			binding.logTextView.text = it
			val scrollY = textLayout.getLineBottom(lineCount - 1) - binding.logTextView.height + binding.logTextView.paddingTop + binding.logTextView.paddingBottom
			binding.logTextView.scrollTo(0, max(scrollY, 0))
		})

		viewModel.state.observe(this, Observer {
			binding.progressBar.visibility = if(it == RegistExecuteViewModel.State.RUNNING) View.VISIBLE else View.GONE
			when(it)
			{
				RegistExecuteViewModel.State.FAILED ->
				{
					binding.infoTextView.visibility = View.VISIBLE
					binding.infoTextView.setText(R.string.regist_info_failed)
					setResult(RESULT_FAILED)
				}
				RegistExecuteViewModel.State.SUCCESSFUL, RegistExecuteViewModel.State.SUCCESSFUL_DUPLICATE ->
				{
					binding.infoTextView.visibility = View.VISIBLE
					binding.infoTextView.setText(R.string.regist_info_success)
					setResult(RESULT_OK)
					if(it == RegistExecuteViewModel.State.SUCCESSFUL_DUPLICATE)
						showDuplicateDialog()
				}
				RegistExecuteViewModel.State.STOPPED ->
				{
					binding.infoTextView.visibility = View.GONE
					setResult(Activity.RESULT_CANCELED)
				}
				else -> binding.infoTextView.visibility = View.GONE
			}
		})

		binding.shareLogButton.setOnClickListener {
			val log = viewModel.logText.value ?: ""
			Intent(Intent.ACTION_SEND).also {
				it.type = "text/plain"
				it.putExtra(Intent.EXTRA_TEXT, log)
				startActivity(Intent.createChooser(it, resources.getString(R.string.action_share_log)))
			}
		}

		val registInfo = IntentCompat.getParcelableExtra(intent, EXTRA_REGIST_INFO, RegistInfo::class.java)
		if(registInfo == null)
		{
			finish()
			return
		}
		viewModel.start(registInfo,
			if(intent.hasExtra(EXTRA_ASSIGN_MANUAL_HOST_ID))
				intent.getLongExtra(EXTRA_ASSIGN_MANUAL_HOST_ID, 0)
			else
				null)
	}

	override fun onStop()
	{
		super.onStop()
		viewModel.stop()
	}

	private var dialog: AlertDialog? = null

	private fun showDuplicateDialog()
	{
		if(dialog != null)
			return

		val macStr = viewModel.host?.serverMac?.let { MacAddress(it).toString() } ?: ""

		dialog = MaterialAlertDialogBuilder(this)
			.setMessage(getString(R.string.alert_regist_duplicate, macStr))
			.setNegativeButton(R.string.action_regist_discard) { _, _ ->  }
			.setPositiveButton(R.string.action_regist_overwrite) { _, _ ->
				viewModel.saveHost()
			}
			.create()
			.also { it.show() }

	}
}