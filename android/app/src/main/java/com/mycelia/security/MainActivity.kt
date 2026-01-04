package com.mycelia.security

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.mycelia.security.ui.screens.ChatScreen
import com.mycelia.security.ui.screens.ConversationsScreen
import com.mycelia.security.ui.screens.InviteScreen
import com.mycelia.security.ui.screens.SettingsScreen
import com.mycelia.security.ui.theme.MyceliaTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MyceliaAppRoot()
        }
    }
}

@Composable
private fun MyceliaAppRoot() {
    val navController = rememberNavController()
    val context = LocalContext.current
    val app = remember { context.applicationContext as MyceliaApp }
    val factory = remember { MyceliaViewModelFactory(app) }

    MyceliaTheme(darkTheme = isSystemInDarkTheme()) {
        NavHost(navController = navController, startDestination = "conversations") {
            composable("conversations") {
                ConversationsScreen(
                    factory = factory,
                    onOpenConversation = { id -> navController.navigate("chat/$id") },
                    onOpenSettings = { navController.navigate("settings") },
                    onScanInvite = { navController.navigate("invite") }
                )
            }
            composable(
                "chat/{conversationId}",
                arguments = listOf(navArgument("conversationId") { type = NavType.StringType })
            ) {
                ChatScreen(
                    factory = factory,
                    onBack = { navController.popBackStack() },
                    onInvite = { id -> navController.navigate("invite/$id") }
                )
            }
            composable("settings") {
                SettingsScreen(factory = factory, onBack = { navController.popBackStack() })
            }
            composable(
                "invite/{conversationId}",
                arguments = listOf(navArgument("conversationId") { type = NavType.StringType })
            ) {
                InviteScreen(
                    factory = factory,
                    onBack = { navController.popBackStack() },
                    onJoinConversation = { id ->
                        navController.navigate("chat/$id") {
                            popUpTo("conversations") { inclusive = false }
                        }
                    }
                )
            }
            composable("invite") {
                InviteScreen(
                    factory = factory,
                    onBack = { navController.popBackStack() },
                    onJoinConversation = { id ->
                        navController.navigate("chat/$id") {
                            popUpTo("conversations") { inclusive = false }
                        }
                    }
                )
            }
        }
    }
}
