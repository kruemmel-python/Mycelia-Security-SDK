package com.mycelia.security

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.CreationExtras
import androidx.lifecycle.createSavedStateHandle
import com.mycelia.security.ui.ConversationsViewModel
import com.mycelia.security.ui.ChatViewModel
import com.mycelia.security.ui.InviteViewModel
import com.mycelia.security.ui.SettingsViewModel

class MyceliaViewModelFactory(private val app: MyceliaApp) : ViewModelProvider.Factory {
    override fun <T : ViewModel> create(modelClass: Class<T>, extras: CreationExtras): T {
        val savedStateHandle = extras.createSavedStateHandle()
        return when {
            modelClass.isAssignableFrom(ConversationsViewModel::class.java) -> {
                ConversationsViewModel(app.chatRepository)
            }
            modelClass.isAssignableFrom(ChatViewModel::class.java) -> {
                ChatViewModel(
                    app.chatRepository,
                    app.cryptoEngine,
                    app.settingsRepository,
                    app,
                    savedStateHandle
                )
            }
            modelClass.isAssignableFrom(SettingsViewModel::class.java) -> {
                SettingsViewModel(app.settingsRepository)
            }
            modelClass.isAssignableFrom(InviteViewModel::class.java) -> {
                InviteViewModel(app.chatRepository, savedStateHandle)
            }
            else -> throw IllegalArgumentException("Unknown ViewModel class: ${modelClass.name}")
        } as T
    }
}
