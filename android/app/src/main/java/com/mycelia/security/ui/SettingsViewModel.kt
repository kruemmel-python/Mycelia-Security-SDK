package com.mycelia.security.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.mycelia.security.settings.SettingsRepository
import com.mycelia.security.settings.SettingsState
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

class SettingsViewModel(private val repository: SettingsRepository) : ViewModel() {
    val settings: StateFlow<SettingsState> = repository.settingsFlow
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(5000),
            SettingsState("10.0.2.2", 8989, false, false, "", "")
        )

    fun updateHost(host: String) {
        viewModelScope.launch { repository.updateHost(host) }
    }

    fun updatePort(port: Int) {
        viewModelScope.launch { repository.updatePort(port) }
    }

    fun updateCompression(enabled: Boolean) {
        viewModelScope.launch { repository.updateCompression(enabled) }
    }

    fun updateTlsEnabled(enabled: Boolean) {
        viewModelScope.launch { repository.updateTlsEnabled(enabled) }
    }

    fun updateTlsPin(pin: String) {
        viewModelScope.launch { repository.updateTlsPin(pin) }
    }

    fun updateTlsCaPem(pem: String) {
        viewModelScope.launch { repository.updateTlsCaPem(pem) }
    }
}
