package com.mycelia.security.ui.screens

import android.graphics.Bitmap
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
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
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.viewmodel.compose.viewModel
import com.google.zxing.BarcodeFormat
import com.google.zxing.MultiFormatWriter
import com.google.zxing.common.BitMatrix
import com.journeyapps.barcodescanner.BarcodeCallback
import com.journeyapps.barcodescanner.BarcodeResult
import com.journeyapps.barcodescanner.DecoratedBarcodeView
import com.mycelia.security.MyceliaViewModelFactory
import com.mycelia.security.ui.ConversationsViewModel
import com.mycelia.security.ui.InviteViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InviteScreen(
    factory: MyceliaViewModelFactory,
    onBack: () -> Unit,
    onJoinConversation: (String) -> Unit
) {
    val inviteViewModel: InviteViewModel = viewModel(factory = factory)
    val conversationsViewModel: ConversationsViewModel = viewModel(factory = factory)
    val conversation by inviteViewModel.conversation.collectAsState()
    var inviteCode by remember { mutableStateOf("") }
    var chatName by remember { mutableStateOf("Session") }
    var showScanner by remember { mutableStateOf(false) }
    var cameraGranted by remember { mutableStateOf(false) }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        cameraGranted = granted
        showScanner = granted
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Invite / QR") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            if (conversation != null) {
                Text("Invite Code (Base64 Seed)")
                TextField(
                    value = conversation?.seedB64 ?: "",
                    onValueChange = {},
                    readOnly = true,
                    modifier = Modifier.fillMaxWidth()
                )
                QrCodeImage(data = conversation?.seedB64 ?: "")
            }

            Text("Invite Code scannen oder einfügen")
            TextField(
                value = chatName,
                onValueChange = { chatName = it },
                label = { Text("Chat-Name") }
            )
            TextField(
                value = inviteCode,
                onValueChange = { inviteCode = it },
                label = { Text("Invite Code") }
            )
            Button(onClick = {
                permissionLauncher.launch(android.Manifest.permission.CAMERA)
            }) {
                Text("QR Scan starten")
            }
            Button(onClick = {
                val code = inviteCode.trim()
                if (code.isNotEmpty()) {
                    val name = chatName.trim().ifBlank { "Session" }
                    conversationsViewModel.joinConversation(name, code) { id ->
                        onJoinConversation(id)
                    }
                }
            }) {
                Text("Invite verwenden")
            }

            if (showScanner && cameraGranted) {
                AndroidView(
                    factory = { context ->
                        DecoratedBarcodeView(context).apply {
                            decodeContinuous(object : BarcodeCallback {
                                override fun barcodeResult(result: BarcodeResult?) {
                                    val text = result?.text ?: return
                                    inviteCode = text
                                    showScanner = false
                                    pause()
                                }
                            })
                            resume()
                        }
                    },
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp)
                )
            }
        }
    }
}

@Composable
private fun QrCodeImage(data: String) {
    if (data.isBlank()) return
    val matrix = remember(data) {
        MultiFormatWriter().encode(data, BarcodeFormat.QR_CODE, 600, 600)
    }
    val bitmap = remember(matrix) { matrix.toBitmap() }
    Image(bitmap = bitmap.asImageBitmap(), contentDescription = "Invite QR")
}

private fun BitMatrix.toBitmap(): Bitmap {
    val width = width
    val height = height
    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
    for (x in 0 until width) {
        for (y in 0 until height) {
            bitmap.setPixel(x, y, if (get(x, y)) 0xFF000000.toInt() else 0xFFFFFFFF.toInt())
        }
    }
    return bitmap
}
