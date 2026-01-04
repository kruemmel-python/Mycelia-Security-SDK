package com.mycelia.security.ui

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.mycelia.security.data.ChatRepository
import com.mycelia.security.data.ConversationEntity
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn

class InviteViewModel(
    repository: ChatRepository,
    savedStateHandle: SavedStateHandle
) : ViewModel() {
    private val conversationId: String? = savedStateHandle.get<String>("conversationId")

    val conversation: StateFlow<ConversationEntity?> =
        if (conversationId != null) {
            repository.observeConversation(conversationId)
                .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), null)
        } else {
            kotlinx.coroutines.flow.flowOf(null)
                .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), null)
        }
}
