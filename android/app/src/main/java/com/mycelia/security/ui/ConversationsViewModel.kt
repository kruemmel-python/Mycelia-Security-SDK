package com.mycelia.security.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.mycelia.security.crypto.SeedManager
import com.mycelia.security.data.ChatRepository
import com.mycelia.security.data.ConversationEntity
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

class ConversationsViewModel(
    private val repository: ChatRepository
) : ViewModel() {
    val conversations: StateFlow<List<ConversationEntity>> =
        repository.observeConversations().stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    fun createConversation(name: String, onCreated: (String) -> Unit) {
        val seed = SeedManager.generateSeed()
        val seedB64 = SeedManager.encodeSeed(seed)
        viewModelScope.launch {
            val id = repository.createConversation(name, seedB64)
            onCreated(id)
        }
    }

    fun joinConversation(name: String, seedB64: String, onCreated: (String) -> Unit) {
        viewModelScope.launch {
            val id = repository.createConversation(name, seedB64)
            onCreated(id)
        }
    }
}
