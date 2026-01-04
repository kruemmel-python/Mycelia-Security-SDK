package com.mycelia.security.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.mycelia.security.MyceliaViewModelFactory
import com.mycelia.security.ui.ConversationsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConversationsScreen(
    factory: MyceliaViewModelFactory,
    onOpenConversation: (String) -> Unit,
    onOpenSettings: () -> Unit,
    onScanInvite: () -> Unit
) {
    val viewModel: ConversationsViewModel = viewModel(factory = factory)
    val conversations by viewModel.conversations.collectAsState()
    var showCreateDialog by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Mycelia Chats") },
                actions = {
                    IconButton(onClick = onScanInvite) {
                        Icon(Icons.Default.QrCodeScanner, contentDescription = "Scan Invite")
                    }
                    IconButton(onClick = onOpenSettings) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showCreateDialog = true }) {
                Icon(Icons.Default.Add, contentDescription = "New Conversation")
            }
        }
    ) { padding ->
        LazyColumn(modifier = Modifier.fillMaxSize().padding(padding)) {
            items(conversations) { conversation ->
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onOpenConversation(conversation.id) }
                        .padding(16.dp)
                ) {
                    Text(conversation.name, style = MaterialTheme.typography.titleMedium)
                    Text(
                        conversation.lastMessagePreview ?: "Noch keine Nachrichten",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            }
        }
    }

    if (showCreateDialog) {
        ConversationDialog(
            title = "Neue Unterhaltung",
            confirmLabel = "Erstellen",
            onDismiss = { showCreateDialog = false },
            onConfirm = { name, seed ->
                showCreateDialog = false
                if (seed.isBlank()) {
                    viewModel.createConversation(name) { id -> onOpenConversation(id) }
                } else {
                    viewModel.joinConversation(name, seed) { id -> onOpenConversation(id) }
                }
            }
        )
    }

}

@Composable
private fun ConversationDialog(
    title: String,
    confirmLabel: String,
    onDismiss: () -> Unit,
    onConfirm: (String, String) -> Unit
) {
    var name by remember { mutableStateOf("Session") }
    var seed by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                TextField(value = name, onValueChange = { name = it }, label = { Text("Name") })
                TextField(
                    value = seed,
                    onValueChange = { seed = it },
                    label = { Text("Invite Code (optional)") }
                )
                Text(
                    "Leer lassen für neue Seed. Invite Code = Base64 Seed.",
                    style = MaterialTheme.typography.bodySmall
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(name.ifBlank { "Session" }, seed.trim()) }) {
                Text(confirmLabel)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Abbrechen")
            }
        }
    )
}
