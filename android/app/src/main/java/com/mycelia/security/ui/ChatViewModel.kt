package com.mycelia.security.ui

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.mycelia.security.crypto.CounterManager
import com.mycelia.security.crypto.CryptoEngine
import com.mycelia.security.crypto.SeedManager
import com.mycelia.security.data.ChatRepository
import com.mycelia.security.data.ConversationEntity
import com.mycelia.security.data.MessageEntity
import com.mycelia.security.network.ChatPayload
import com.mycelia.security.network.TcpChatClient
import com.mycelia.security.network.CompressionUtils
import com.mycelia.security.settings.SettingsRepository
import com.mycelia.security.settings.SettingsState
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import java.nio.charset.StandardCharsets
import java.util.UUID

class ChatViewModel(
    private val repository: ChatRepository,
    private val cryptoEngine: CryptoEngine,
    private val settingsRepository: SettingsRepository,
    savedStateHandle: SavedStateHandle
) : ViewModel() {
    private val conversationId: String = requireNotNull(savedStateHandle["conversationId"]) {
        "conversationId missing"
    }

    private val conversationState = repository.observeConversation(conversationId)
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), null)
    private val messagesFlow = repository.observeMessages(conversationId)
    private val _connectionState = MutableStateFlow<TcpChatClient.ConnectionState>(TcpChatClient.ConnectionState.Disconnected)
    private val compressionEnabled = MutableStateFlow(false)

    val uiState: StateFlow<ChatUiState> = combine(
        conversationState,
        messagesFlow,
        _connectionState
    ) { conversation, messages, connection ->
        ChatUiState(conversation, messages, connection)
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), ChatUiState(null, emptyList(), TcpChatClient.ConnectionState.Disconnected))

    private var clientJob: Job? = null
    private var currentClient: TcpChatClient? = null

    init {
        viewModelScope.launch {
            settingsRepository.settingsFlow.collect { settings ->
                compressionEnabled.value = settings.compressionEnabled
                restartClient(settings)
            }
        }
    }

    private fun restartClient(settings: SettingsState) {
        clientJob?.cancel()
        currentClient?.stop()
        currentClient = null
        clientJob = viewModelScope.launch {
            conversationState.filterNotNull().distinctUntilChangedBy { it.seedB64 }.collect { conversation ->
                currentClient?.stop()
                val client = TcpChatClient(
                    host = settings.host,
                    port = settings.port,
                    roomId = conversation.seedB64
                )
                currentClient = client
                client.start()
                viewModelScope.launch {
                    client.connectionState.collect { _connectionState.value = it }
                }
                viewModelScope.launch {
                    client.incoming.collect { payload ->
                        if (payload is ChatPayload.Message) {
                            handleIncoming(conversation, payload)
                        }
                    }
                }
            }
        }
    }

    fun sendMessage(text: String) {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return
        viewModelScope.launch {
            val conversation = conversationState.value ?: return@launch
            val seed = SeedManager.decodeSeed(conversation.seedB64)
            val counter = conversation.lastCounter + 1
            val streamOffset = CounterManager.streamOffset(counter, 0)
            val cipherRaw = cryptoEngine.encrypt(trimmed.toByteArray(StandardCharsets.UTF_8), seed, streamOffset)
            val cipher = if (compressionEnabled.value) {
                CompressionUtils.compress(cipherRaw)
            } else {
                cipherRaw
            }
            val payload = ChatPayload.Message(conversation.seedB64, cipher, counter)
            currentClient?.sendMessage(payload)

            val preview = trimmed.take(200)
            repository.insertMessage(
                MessageEntity(
                    id = UUID.randomUUID().toString(),
                    conversationId = conversation.id,
                    timestamp = System.currentTimeMillis(),
                    direction = "OUT",
                    plaintextPreview = preview,
                    ciphertextHexOptional = null,
                    counterUsed = counter
                )
            )
            repository.updateConversationCounter(conversation.id, counter)
        }
    }

    private suspend fun handleIncoming(conversation: ConversationEntity, payload: ChatPayload.Message) {
        if (payload.roomId != conversation.seedB64) return
        val seed = SeedManager.decodeSeed(conversation.seedB64)
        val streamOffset = CounterManager.streamOffset(payload.counter, 0)
        val cipher = if (compressionEnabled.value) {
            CompressionUtils.decompress(payload.bodyCipher)
        } else {
            payload.bodyCipher
        }
        val plainBytes = cryptoEngine.decrypt(cipher, seed, streamOffset)
        val text = plainBytes.toString(StandardCharsets.UTF_8)
        val preview = text.take(200)
        repository.insertMessage(
            MessageEntity(
                id = UUID.randomUUID().toString(),
                conversationId = conversation.id,
                timestamp = System.currentTimeMillis(),
                direction = "IN",
                plaintextPreview = preview,
                ciphertextHexOptional = null,
                counterUsed = payload.counter
            )
        )
        repository.updateConversationCounter(conversation.id, payload.counter)
    }

    fun wipeConversation() {
        viewModelScope.launch {
            repository.deleteConversation(conversationId)
        }
    }

    override fun onCleared() {
        super.onCleared()
        currentClient?.stop()
    }
}

data class ChatUiState(
    val conversation: ConversationEntity?,
    val messages: List<MessageEntity>,
    val connectionState: TcpChatClient.ConnectionState
)
