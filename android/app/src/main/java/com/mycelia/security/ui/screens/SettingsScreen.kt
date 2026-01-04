package com.mycelia.security.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.mycelia.security.MyceliaViewModelFactory
import com.mycelia.security.ui.SettingsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(factory: MyceliaViewModelFactory, onBack: () -> Unit) {
    val viewModel: SettingsViewModel = viewModel(factory = factory)
    val settings by viewModel.settings.collectAsState()
    var host by remember(settings.host) { mutableStateOf(settings.host) }
    var port by remember(settings.port) { mutableStateOf(settings.port.toString()) }
    var tlsPin by remember(settings.tlsPinSha256) { mutableStateOf(settings.tlsPinSha256) }
    var tlsCaPem by remember(settings.tlsCaPem) { mutableStateOf(settings.tlsCaPem) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Einstellungen") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(modifier = Modifier.fillMaxSize().padding(padding).padding(16.dp)) {
            TextField(
                value = host,
                onValueChange = {
                    host = it
                    viewModel.updateHost(it)
                },
                label = { Text("Server Host") }
            )
            TextField(
                value = port,
                onValueChange = {
                    port = it
                    it.toIntOrNull()?.let { value -> viewModel.updatePort(value) }
                },
                label = { Text("Server Port") }
            )
            Column(modifier = Modifier.padding(top = 16.dp)) {
                Text("Kompression (zlib)")
                Switch(
                    checked = settings.compressionEnabled,
                    onCheckedChange = { viewModel.updateCompression(it) }
                )
            }
            Column(modifier = Modifier.padding(top = 16.dp)) {
                Text("TLS aktivieren")
                Switch(
                    checked = settings.tlsEnabled,
                    onCheckedChange = { viewModel.updateTlsEnabled(it) }
                )
                TextField(
                    value = tlsPin,
                    onValueChange = {
                        tlsPin = it
                        viewModel.updateTlsPin(it.trim())
                    },
                    label = { Text("TLS Pin (SHA-256, hex)") }
                )
                TextField(
                    value = tlsCaPem,
                    onValueChange = {
                        tlsCaPem = it
                        viewModel.updateTlsCaPem(it)
                    },
                    label = { Text("TLS CA PEM") },
                    modifier = Modifier.padding(top = 8.dp)
                )
            }
        }
    }
}
