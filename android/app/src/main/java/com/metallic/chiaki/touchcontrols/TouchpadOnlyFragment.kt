// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

package com.metallic.chiaki.touchcontrols

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.lifecycle.LiveData
import androidx.lifecycle.Observer
import com.metallic.chiaki.databinding.FragmentTouchpadOnlyBinding
import kotlinx.coroutines.flow.combine

class TouchpadOnlyFragment : TouchControlsFragment()
{
	var touchpadOnlyEnabled: LiveData<Boolean>? = null

	private var _binding: FragmentTouchpadOnlyBinding? = null
	private val binding get() = _binding!!

	override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View =
		FragmentTouchpadOnlyBinding.inflate(inflater, container, false).let {
			_binding = it
			_controllerStateSource.value =
				combine(_ownControllerStateFlow, binding.touchpadView.controllerState) { a, b -> a or b }
			it.root
		}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?)
	{
		super.onViewCreated(view, savedInstanceState)
		touchpadOnlyEnabled?.observe(viewLifecycleOwner, Observer {
			view.visibility = if(it) View.VISIBLE else View.GONE
		})
	}

	private fun buttonStateChanged(buttonMask: UInt) = { pressed: Boolean ->
		ownControllerState = ownControllerState.copy().apply {
			buttons =
				if(pressed)
					buttons or buttonMask
				else
					buttons and buttonMask.inv()
		}
	}
}
