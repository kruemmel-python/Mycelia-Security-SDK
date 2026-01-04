# Mycelia Android (Vulkan Compute)

Dieses Verzeichnis enthält das Android/NDK‑Gerüst für den Mycelia‑Treiber (GPU‑Kern via Vulkan Compute) und den Chat/Vault‑Client.

## Überblick

- **NDK/JNI**: Native Bibliothek für Verschlüsselung/Entschlüsselung.
- **Vulkan Compute**: Keystream‑Anwendung auf dem GPU‑Pfad.
- **MCP/Zlib**: 1:1 Protokoll‑ und Kompressionskompatibilität wie in `python/mycelia_chat.py`.

## Build (NDK)

In Android Studio:

1. `android/` als Projekt öffnen.
2. NDK und CMake installieren.
3. Build ausführen. Es wird eine native Bibliothek `libmycelia_native.so` erzeugt.

## Hinweise

- Der Vulkan‑Compute‑Pfad ist vorbereitet und wird für die Keystream‑Anwendung genutzt.
- Für eine vollständige 1:1 Kompatibilität müssen die Keystream‑Kernels exakt die Logik der bisherigen OpenCL‑Kerne abbilden.

## Dateien

- `app/src/main/cpp/mycelia_vulkan_compute.*`: Vulkan Compute Pipeline + Buffer‑Handling.
- `app/src/main/cpp/mycelia_jni.cpp`: JNI‑Bridge für Java/Kotlin.
- `app/src/main/shaders/mycelia_xor.comp`: Compute‑Shader (XOR‑Anwendung).
