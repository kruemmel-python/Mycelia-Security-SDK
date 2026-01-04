package com.mycelia.security.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.QrCode
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.mycelia.security.MyceliaViewModelFactory
import com.mycelia.security.data.MessageEntity
import com.mycelia.security.network.TcpChatClient
import com.mycelia.security.ui.ChatViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatScreen(
    factory: MyceliaViewModelFactory,
    onBack: () -> Unit,
    onInvite: (String) -> Unit
) {
    val viewModel: ChatViewModel = viewModel(factory = factory)
    val state by viewModel.uiState.collectAsState()
    var messageText by remember { mutableStateOf("") }
    var showWipeDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(state.conversation?.name ?: "Chat") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    state.conversation?.let { convo ->
                        IconButton(onClick = { onInvite(convo.id) }) {
                            Icon(Icons.Default.QrCode, contentDescription = "Invite")
                        }
                    }
                    IconButton(onClick = { showWipeDialog = true }) {
                        Icon(Icons.Default.Delete, contentDescription = "Wipe")
                    }
                }
            )
        }
    ) { padding ->
        Column(modifier = Modifier.fillMaxSize().padding(padding)) {
            ConnectionStatus(state.connectionState)
            LazyColumn(
                modifier = Modifier.weight(1f).fillMaxWidth().padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(state.messages) { message ->
                    MessageBubble(message)
                }
            }
            Row(modifier = Modifier.fillMaxWidth().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
                TextField(
                    value = messageText,
                    onValueChange = { messageText = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Nachricht") }
                )
                Spacer(modifier = Modifier.width(8.dp))
                Button(onClick = {
                    viewModel.sendMessage(messageText)
                    messageText = ""
                }) {
                    Text("Senden")
                }
            }
        }
    }

    if (showWipeDialog) {
        AlertDialog(
            onDismissRequest = { showWipeDialog = false },
            title = { Text("Chat löschen") },
            text = { Text("Seed und Nachrichten werden gelöscht. Fortfahren?") },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.wipeConversation()
                    showWipeDialog = false
                    onBack()
                }) {
                    Text("Löschen")
                }
            },
            dismissButton = {
                TextButton(onClick = { showWipeDialog = false }) {
                    Text("Abbrechen")
                }
            }
        )
    }
}

@Composable
private fun MessageBubble(message: MessageEntity) {
    val alignment = if (message.direction == "OUT") Alignment.End else Alignment.Start
    Column(modifier = Modifier.fillMaxWidth(), horizontalAlignment = alignment) {
        Text(
            text = message.plaintextPreview,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.padding(12.dp)
        )
    }
}

@Composable
private fun ConnectionStatus(state: TcpChatClient.ConnectionState) {
    val text = when (state) {
        TcpChatClient.ConnectionState.Connected -> "Verbunden"
        TcpChatClient.ConnectionState.Disconnected -> "Getrennt"
        is TcpChatClient.ConnectionState.Error -> "Fehler: ${state.message}"
    }
    Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        modifier = Modifier.padding(8.dp)
    )
}
