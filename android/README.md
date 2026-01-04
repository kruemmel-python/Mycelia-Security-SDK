# Mycelia Android Chat (Vulkan Compute)

Dieses Verzeichnis enthält eine vollständige Android-Chat-App (Jetpack Compose) mit dem Mycelia Vulkan-Compute JNI-Pfad.

## Überblick

- **NDK/JNI**: `libmycelia_native.so` mit `com.mycelia.security.MyceliaNative`
- **Vulkan Compute**: GPU-Keystream XOR (SPIR-V Shader)
- **Protokoll**: TCP + Length-Prefix Framing + JSON Payload
- **Kompression**: Optional zlib (Feature-Flag in den Einstellungen)
- **Persistenz**: Room DB (`conversations`, `messages`)
- **Seed-Sharing**: Invite Code (Base64 Seed) + QR

## Shader (SPIR-V)

Die GLSL-Shader liegen in `android/shaders_src/`. Beim Build werden sie per Gradle-Task
`compileMyceliaShaders` mit dem NDK-Tool `glslc` nach `app/src/main/assets/shaders/`
kompiliert. Stelle sicher, dass das NDK (Version passend zu `android.ndkVersion`) installiert ist.

## Build & Run (Android Studio)

1. `android/` als Projekt öffnen.
2. NDK + CMake installieren.
3. Sync/Build ausführen.
4. App starten.

## Server starten

```bash
python3 tools/server/mycelia_chat_server.py
```

Standard-Port: `8989`.

## App konfigurieren

In **Einstellungen**:
- **Server Host**: z. B. `10.0.2.2` (Android Emulator) oder die LAN-IP des Hosts.
- **Server Port**: `8989`.
- **Kompression**: Muss auf beiden Clients gleich gesetzt sein.

## Invite / QR Workflow

1. Neue Unterhaltung erstellen (Seed wird generiert).
2. Invite Screen öffnen und Base64 Seed oder QR-Code teilen.
3. Gegenstelle scannt QR oder fügt Invite Code ein.

## Troubleshooting

- **Firewall**: Port `8989` freigeben.
- **Emulator**: `10.0.2.2` für Host-Loopback nutzen.
- **Reales Gerät**: Gerät und Server im gleichen WLAN, Host-IP eintragen.
- **Vulkan/Shader**: Prüfen, ob `mycelia_keystream_xor.spv` in Assets vorhanden ist.
