package com.mycelia.security.ui

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.mycelia.security.crypto.CryptoEngine
import com.mycelia.security.crypto.Hkdf
import com.mycelia.security.crypto.KeyExchange
import com.mycelia.security.crypto.SeedManager
import com.mycelia.security.data.ChatRepository
import com.mycelia.security.data.ConversationEntity
import com.mycelia.security.data.MessageEntity
import com.mycelia.security.network.ChatPayload
import com.mycelia.security.network.TcpChatClient
import com.mycelia.security.network.CompressionUtils
import com.mycelia.security.settings.SettingsRepository
import com.mycelia.security.settings.SettingsState
import java.nio.ByteBuffer
import java.nio.ByteOrder
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
    private val sentCounters = mutableSetOf<Long>()
    private var sessionKey: ByteArray? = null
    private val pendingMessages = mutableListOf<String>()
    private var localCounter = 0L

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
        viewModelScope.launch {
            conversationState.filterNotNull().collect { conversation ->
                if (conversation.remotePublicKeyB64 != null) {
                    deriveSessionKey(conversation)
                }
                if (conversation.lastCounter > localCounter) {
                    localCounter = conversation.lastCounter
                }
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
                    roomId = conversation.seedB64,
                    tlsEnabled = settings.tlsEnabled,
                    tlsPinSha256 = settings.tlsPinSha256
                )
                currentClient = client
                client.start()
                viewModelScope.launch {
                    client.connectionState.collect { _connectionState.value = it }
                }
                viewModelScope.launch {
                    client.incoming.collect { payload ->
                        when (payload) {
                            is ChatPayload.Hello -> handleHello(conversation, payload)
                            is ChatPayload.Message -> handleIncoming(conversation, payload)
                            else -> Unit
                        }
                    }
                }
                sendHello(conversation)
            }
        }
    }

    fun sendMessage(text: String) {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return
        viewModelScope.launch {
            val conversation = conversationState.value ?: return@launch
            val key = sessionKey ?: run {
                pendingMessages.add(trimmed)
                sendHello(conversation)
                _connectionState.value = TcpChatClient.ConnectionState.Error("Schlüsselaustausch ausstehend")
                return@launch
            }
            sendMessageInternal(conversation, key, trimmed)
        }
    }

    private suspend fun handleIncoming(conversation: ConversationEntity, payload: ChatPayload.Message) {
        if (payload.roomId != conversation.seedB64) return
        if (sentCounters.remove(payload.counter)) {
            return
        }
        if (payload.counter <= conversation.lastCounter) {
            return
        }
        if (payload.counter > localCounter) {
            localCounter = payload.counter
        }
        val key = sessionKey ?: return
        val cipher = if (compressionEnabled.value) {
            CompressionUtils.decompress(payload.bodyCipher)
        } else {
            payload.bodyCipher
        }
        val nonce = buildNonce(payload.counter)
        val aad = buildAad(conversation.seedB64, payload.counter)
        val plainBytes = cryptoEngine.aeadDecrypt(key, nonce, cipher, aad)
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

    private suspend fun handleHello(conversation: ConversationEntity, payload: ChatPayload.Hello) {
        if (payload.roomId != conversation.seedB64) return
        val seed = SeedManager.decodeSeed(conversation.seedB64)
        val expectedMac = KeyExchange.computeHelloMac(seed, payload.publicKey)
        if (!expectedMac.contentEquals(payload.mac)) return
        if (payload.publicKey.contentEquals(KeyExchange.decodeKey(conversation.localPublicKeyB64))) {
            return
        }
        val remoteKeyB64 = KeyExchange.encodeKey(payload.publicKey)
        repository.updateRemoteKey(conversation.id, remoteKeyB64)
        deriveSessionKey(conversation.copy(remotePublicKeyB64 = remoteKeyB64))
    }

    private fun sendHello(conversation: ConversationEntity) {
        val seed = SeedManager.decodeSeed(conversation.seedB64)
        val pubKey = KeyExchange.decodeKey(conversation.localPublicKeyB64)
        val mac = KeyExchange.computeHelloMac(seed, pubKey)
        viewModelScope.launch {
            currentClient?.sendMessage(ChatPayload.Hello(conversation.seedB64, pubKey, mac))
        }
        if (conversation.remotePublicKeyB64 != null) {
            deriveSessionKey(conversation)
        }
    }

    private fun deriveSessionKey(conversation: ConversationEntity) {
        val remote = conversation.remotePublicKeyB64 ?: return
        val localPrivate = KeyExchange.decodeKey(conversation.localPrivateKeyB64)
        val remotePublic = KeyExchange.decodeKey(remote)
        val shared = KeyExchange.deriveSharedSecret(localPrivate, remotePublic)
        val seed = SeedManager.decodeSeed(conversation.seedB64)
        val info = "mycelia-aead".toByteArray(StandardCharsets.UTF_8)
        sessionKey = Hkdf.deriveKey(shared, seed, info, 32)
        flushPending(conversation)
    }

    private fun nextCounter(conversation: ConversationEntity): Long {
        if (localCounter < conversation.lastCounter) {
            localCounter = conversation.lastCounter
        }
        localCounter += 1
        return localCounter
    }

    private suspend fun sendMessageInternal(conversation: ConversationEntity, key: ByteArray, text: String) {
        val counter = nextCounter(conversation)
        val nonce = buildNonce(counter)
        val aad = buildAad(conversation.seedB64, counter)
        val plainBytes = text.toByteArray(StandardCharsets.UTF_8)
        val cipherRaw = cryptoEngine.aeadEncrypt(key, nonce, plainBytes, aad)
        val cipher = if (compressionEnabled.value) {
            CompressionUtils.compress(cipherRaw)
        } else {
            cipherRaw
        }
        val payload = ChatPayload.Message(conversation.seedB64, cipher, counter)
        currentClient?.sendMessage(payload)
        sentCounters.add(counter)

        val preview = text.take(200)
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

    private fun flushPending(conversation: ConversationEntity) {
        val key = sessionKey ?: return
        if (pendingMessages.isEmpty()) return
        val queue = pendingMessages.toList()
        pendingMessages.clear()
        viewModelScope.launch {
            for (message in queue) {
                sendMessageInternal(conversation, key, message)
            }
        }
    }

    private fun buildNonce(counter: Long): ByteArray {
        val buffer = ByteBuffer.allocate(12).order(ByteOrder.BIG_ENDIAN)
        buffer.putInt(0)
        buffer.putLong(counter)
        return buffer.array()
    }

    private fun buildAad(roomId: String, counter: Long): ByteArray {
        return "$roomId:$counter".toByteArray(StandardCharsets.UTF_8)
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
