package com.mycelia.security.data

import androidx.room.Entity
import androidx.room.ForeignKey
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(tableName = "conversations")
data class ConversationEntity(
    @PrimaryKey val id: String,
    val name: String,
    val createdAt: Long,
    val seedB64: String,
    val localPublicKeyB64: String,
    val localPrivateKeyB64: String,
    val remotePublicKeyB64: String?,
    val lastCounter: Long,
    val lastMessagePreview: String?
)

@Entity(
    tableName = "messages",
    foreignKeys = [
        ForeignKey(
            entity = ConversationEntity::class,
            parentColumns = ["id"],
            childColumns = ["conversationId"],
            onDelete = ForeignKey.CASCADE
        )
    ],
    indices = [Index("conversationId")]
)
data class MessageEntity(
    @PrimaryKey val id: String,
    val conversationId: String,
    val timestamp: Long,
    val direction: String,
    val plaintextPreview: String,
    val ciphertextHexOptional: String?,
    val counterUsed: Long
)
