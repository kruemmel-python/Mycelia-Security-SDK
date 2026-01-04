package com.mycelia.security

import android.app.Application
import com.mycelia.security.crypto.CryptoEngine
import com.mycelia.security.data.AppDatabase
import com.mycelia.security.data.ChatRepository
import com.mycelia.security.settings.SettingsRepository

class MyceliaApp : Application() {
    val database: AppDatabase by lazy { AppDatabase.get(this) }
    val chatRepository: ChatRepository by lazy {
        ChatRepository(database.conversationDao(), database.messageDao())
    }
    val settingsRepository: SettingsRepository by lazy { SettingsRepository(this) }
    val cryptoEngine: CryptoEngine by lazy { CryptoEngine(this) }
}
