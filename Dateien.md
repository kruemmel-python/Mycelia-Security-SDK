# Dateienübersicht – notwendige Verzeichnisse & Dateien für Build/Run

Diese Übersicht listet alle Verzeichnisse und Dateien, die im Repository vorhanden sein müssen, um **Server** und **Android‑App** erfolgreich zu bauen und auszuführen. Der Baum ist bewusst auf die relevanten Pfade reduziert.

```
.
├─ android/
│  ├─ settings.gradle.kts
│  ├─ build.gradle.kts
│  ├─ gradle.properties
│  ├─ app/
│  │  ├─ build.gradle.kts
│  │  ├─ proguard-rules.pro
│  │  └─ src/
│  │     ├─ main/
│  │     │  ├─ AndroidManifest.xml
│  │     │  ├─ assets/
│  │     │  │  └─ shaders/
│  │     │  │     └─ mycelia_keystream_xor.spv
│  │     │  ├─ cpp/
│  │     │  │  ├─ CMakeLists.txt
│  │     │  │  ├─ mycelia_jni.cpp
│  │     │  │  ├─ mycelia_vulkan_compute.cpp
│  │     │  │  └─ mycelia_vulkan_compute.h
│  │     │  ├─ java/
│  │     │  │  └─ com/mycelia/security/
│  │     │  │     ├─ MainActivity.kt
│  │     │  │     ├─ MyceliaApp.kt
│  │     │  │     ├─ MyceliaViewModelFactory.kt
│  │     │  │     ├─ crypto/
│  │     │  │     │  ├─ AeadCipher.kt
│  │     │  │     │  ├─ CounterManager.kt
│  │     │  │     │  ├─ CryptoEngine.kt
│  │     │  │     │  ├─ Hkdf.kt
│  │     │  │     │  ├─ KeyExchange.kt
│  │     │  │     │  ├─ MyceliaNative.kt
│  │     │  │     │  ├─ SeedManager.kt
│  │     │  │     │  └─ SubqgRng.kt
│  │     │  │     ├─ data/
│  │     │  │     │  ├─ AppDatabase.kt
│  │     │  │     │  ├─ ChatRepository.kt
│  │     │  │     │  ├─ Dao.kt
│  │     │  │     │  ├─ DatabaseKeyManager.kt
│  │     │  │     │  └─ Entities.kt
│  │     │  │     ├─ network/
│  │     │  │     │  ├─ ChatProtocol.kt
│  │     │  │     │  ├─ CompressionUtils.kt
│  │     │  │     │  ├─ Framing.kt
│  │     │  │     │  └─ TcpChatClient.kt
│  │     │  │     ├─ settings/
│  │     │  │     │  └─ SettingsRepository.kt
│  │     │  │     ├─ ui/
│  │     │  │     │  ├─ ChatViewModel.kt
│  │     │  │     │  ├─ ConversationsViewModel.kt
│  │     │  │     │  ├─ InviteViewModel.kt
│  │     │  │     │  ├─ SettingsViewModel.kt
│  │     │  │     │  └─ screens/
│  │     │  │     │     ├─ ChatScreen.kt
│  │     │  │     │     ├─ ConversationsScreen.kt
│  │     │  │     │     ├─ InviteScreen.kt
│  │     │  │     │     └─ SettingsScreen.kt
│  │     │  │     └─ ui/theme/
│  │     │  │        └─ Theme.kt
│  │     │  ├─ res/
│  │     │  │  └─ values/
│  │     │  │     └─ themes.xml
│  │     └─ test/
│  │        └─ java/com/mycelia/security/
│  │           ├─ CounterManagerTest.kt
│  │           ├─ FramingTest.kt
│  │           └─ SeedManagerTest.kt
│  └─ shaders_src/
│     ├─ mycelia_keystream_xor.comp
│     ├─ mycelia_xor.comp
│     ├─ subqg_init.comp
│     └─ subqg_simulation.comp
├─ tools/
│  └─ server/
│     ├─ mycelia_chat_server.py
│     └─ README.md
├─ Dokumentation.md
├─ Whitepaper.md
└─ server_tls.md
```

## Hinweise

- **Shader‑Build:** Die SPIR‑V Datei `mycelia_keystream_xor.spv` muss im `assets/shaders/` Pfad liegen. Falls nicht vorhanden, muss sie via `compileMyceliaShaders` erzeugt werden.
- **Server:** Der Relay‑Server liegt unter `tools/server/` und benötigt nur Python 3.12+.
- **Dokumentation:** `Dokumentation.md`, `Whitepaper.md` und `server_tls.md` gehören zur Auslieferung (Build‑ und Security‑Dokumente).
