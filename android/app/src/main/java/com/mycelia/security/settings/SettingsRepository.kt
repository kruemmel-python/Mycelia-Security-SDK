package com.mycelia.security.settings

import android.content.Context
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore by preferencesDataStore(name = "settings")

class SettingsRepository(private val context: Context) {
    private val hostKey = stringPreferencesKey("host")
    private val portKey = intPreferencesKey("port")
    private val compressionKey = booleanPreferencesKey("compression")
    private val tlsKey = booleanPreferencesKey("tls")
    private val pinKey = stringPreferencesKey("tls_pin")
    private val caKey = stringPreferencesKey("tls_ca_pem")

    val settingsFlow: Flow<SettingsState> = context.dataStore.data.map { prefs ->
        SettingsState(
            host = prefs[hostKey] ?: "10.0.2.2",
            port = prefs[portKey] ?: 8989,
            compressionEnabled = prefs[compressionKey] ?: false,
            tlsEnabled = prefs[tlsKey] ?: false,
            tlsPinSha256 = prefs[pinKey] ?: "",
            tlsCaPem = prefs[caKey] ?: ""
        )
    }

    suspend fun updateHost(host: String) {
        context.dataStore.edit { it[hostKey] = host }
    }

    suspend fun updatePort(port: Int) {
        context.dataStore.edit { it[portKey] = port }
    }

    suspend fun updateCompression(enabled: Boolean) {
        context.dataStore.edit { it[compressionKey] = enabled }
    }

    suspend fun updateTlsEnabled(enabled: Boolean) {
        context.dataStore.edit { it[tlsKey] = enabled }
    }

    suspend fun updateTlsPin(pin: String) {
        context.dataStore.edit { it[pinKey] = pin }
    }

    suspend fun updateTlsCaPem(pem: String) {
        context.dataStore.edit { it[caKey] = pem }
    }
}

data class SettingsState(
    val host: String,
    val port: Int,
    val compressionEnabled: Boolean,
    val tlsEnabled: Boolean,
    val tlsPinSha256: String,
    val tlsCaPem: String
)
