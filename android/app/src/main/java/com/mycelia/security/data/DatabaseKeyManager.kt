package com.mycelia.security.data

import android.content.Context
import android.util.Base64
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import java.security.SecureRandom

object DatabaseKeyManager {
    private const val PREFS_NAME = "db_keys"
    private const val KEY_NAME = "room_passphrase"

    fun getOrCreatePassphrase(context: Context): ByteArray {
        val masterKey = MasterKey.Builder(context)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        val prefs = EncryptedSharedPreferences.create(
            context,
            PREFS_NAME,
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )
        val stored = prefs.getString(KEY_NAME, null)
        if (stored != null) {
            return Base64.decode(stored, Base64.NO_WRAP)
        }
        val random = SecureRandom()
        val bytes = ByteArray(32)
        random.nextBytes(bytes)
        prefs.edit().putString(KEY_NAME, Base64.encodeToString(bytes, Base64.NO_WRAP)).apply()
        return bytes
    }
}
