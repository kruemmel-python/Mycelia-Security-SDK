# Mycelia Android (Vulkan Compute)

Dieses Verzeichnis enthält das Android/NDK‑Gerüst für den Mycelia‑Treiber (GPU‑Kern via Vulkan Compute) und den Chat/Vault‑Client.

## Überblick

- **NDK/JNI**: Native Bibliothek für Verschlüsselung/Entschlüsselung.
- **Vulkan Compute**: 1:1 Port der OpenCL‑Keystream‑Logik (SubQG Simulation + Convert‑to‑Bytes + XOR).
- **MCP/Zlib**: 1:1 Protokoll‑ und Kompressionskompatibilität wie in `python/mycelia_chat.py`.

## Shader (SPIR-V) Build – Einzeiler

```bash
glslangValidator -V android/app/src/main/shaders/subqg_init.comp -o android/app/src/main/shaders/subqg_init.spv && glslangValidator -V android/app/src/main/shaders/subqg_simulation.comp -o android/app/src/main/shaders/subqg_simulation.spv && glslangValidator -V android/app/src/main/shaders/mycelia_keystream_xor.comp -o android/app/src/main/shaders/mycelia_keystream_xor.spv
```

## Build (NDK)

In Android Studio:

1. `android/` als Projekt öffnen.
2. NDK und CMake installieren.
3. Build ausführen. Es wird eine native Bibliothek `libmycelia_native.so` erzeugt.

## Hinweise

- Der Vulkan‑Compute‑Pfad benötigt die SPIR‑V Dateien neben den GLSL‑Shadern.
- Für 1:1 Kompatibilität muss der `subqg_simulation`‑Shader bytegenau dem OpenCL‑Kern entsprechen.
- Der JNI‑Aufruf erwartet einen Shader‑Pfad (z. B. App‑internes Files‑Dir mit den SPIR‑V Dateien).

## Dateien

- `app/src/main/cpp/mycelia_vulkan_compute.*`: Vulkan Compute Pipeline + Buffer‑Handling.
- `app/src/main/cpp/mycelia_jni.cpp`: JNI‑Bridge für Java/Kotlin.
- `app/src/main/shaders/subqg_init.comp`: Init‑Kernel (entspricht OpenCL‑Buffer‑Init).
- `app/src/main/shaders/subqg_simulation.comp`: SubQG Simulation (1:1 Port).
- `app/src/main/shaders/mycelia_keystream_xor.comp`: Keystream‑Hash + XOR.
