package com.mycelia.security.data

import kotlinx.coroutines.flow.Flow
import java.util.UUID

class ChatRepository(
    private val conversationDao: ConversationDao,
    private val messageDao: MessageDao
) {
    fun observeConversations(): Flow<List<ConversationEntity>> = conversationDao.observeAll()

    fun observeConversation(id: String): Flow<ConversationEntity?> = conversationDao.observeById(id)

    fun observeMessages(conversationId: String): Flow<List<MessageEntity>> =
        messageDao.observeByConversation(conversationId)

    suspend fun createConversation(
        name: String,
        seedB64: String,
        localPublicKeyB64: String,
        localPrivateKeyB64: String
    ): String {
        val id = UUID.randomUUID().toString()
        val conversation = ConversationEntity(
            id = id,
            name = name,
            createdAt = System.currentTimeMillis(),
            seedB64 = seedB64,
            localPublicKeyB64 = localPublicKeyB64,
            localPrivateKeyB64 = localPrivateKeyB64,
            remotePublicKeyB64 = null,
            lastCounter = 0L,
            lastMessagePreview = null
        )
        conversationDao.insert(conversation)
        return id
    }

    suspend fun insertMessage(message: MessageEntity) {
        messageDao.insert(message)
        conversationDao.updatePreview(message.conversationId, message.plaintextPreview, message.counterUsed)
    }

    suspend fun updateConversationCounter(id: String, counter: Long) {
        val existing = conversationDao.getById(id)
        if (existing != null && counter > existing.lastCounter) {
            conversationDao.update(existing.copy(lastCounter = counter))
        }
    }

    suspend fun updateRemoteKey(id: String, remotePublicKeyB64: String) {
        conversationDao.updateRemoteKey(id, remotePublicKeyB64)
    }

    suspend fun deleteConversation(id: String) {
        messageDao.deleteByConversation(id)
        conversationDao.deleteById(id)
    }
}
