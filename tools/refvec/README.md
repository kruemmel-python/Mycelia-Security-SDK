# Reference Vector Generator (ChaCha20) & Vulkan Comparator

Dieses Verzeichnis enthält ein Referenz-CLI für den ChaCha20‑Pfad (CPU) und einen Vulkan‑Runner, um die Keystream‑Ergebnisse bytegenau zu prüfen.

## Dateien

- `refvec_cli.c`: CPU‑Referenz (HKDF‑SHA256 + ChaCha20) zur Vektor‑Erzeugung.
- `vulkan_runner.cpp`: nutzt `MyceliaVulkanCompute` (Vulkan Compute) zur Vergleichsausführung.
- `gen_vectors.py`: generiert `tests/vectors/generated_vectors.json` aus `tests/vectors/test_cases.json`.
- `compare_vectors.py`: vergleicht Vulkan‑Output mit Referenzvektoren und prüft Round‑Trip (decrypt(encrypt)).

## Build (CPU + Vulkan) – Einzeiler

```bash
cc -O2 -o refvec_cli refvec_cli.c
```

```bash
g++ -O2 -std=c++17 -I../../android/app/src/main/cpp -I../../include -o vulkan_runner vulkan_runner.cpp ../../android/app/src/main/cpp/mycelia_vulkan_compute.cpp -lvulkan
```

## Shader Build (SPIR-V) – Einzeiler

```bash
glslangValidator -V ../../android/app/src/main/shaders/mycelia_keystream_xor.comp -o shaders/mycelia_keystream_xor.spv
```

## Vektoren erzeugen – Einzeiler

```bash
python3 gen_vectors.py
```

## Vergleich Vulkan vs CPU‑Referenz – Einzeiler

```bash
python3 compare_vectors.py ./shaders
```
