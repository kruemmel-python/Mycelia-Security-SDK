# Reference Vector Generator (OpenCL) & Vulkan Comparator

Dieses Verzeichnis enthält ein Referenz-CLI für den OpenCL‑Pfad und einen Vulkan‑Runner, um die 1:1‑Kompatibilität zu prüfen.

## Dateien

- `refvec_cli.c`: nutzt `myc_process_buffer` (OpenCL) zur Vektor‑Erzeugung.
- `vulkan_runner.cpp`: nutzt `MyceliaVulkanCompute` (Vulkan Compute) zur Vergleichsausführung.
- `gen_vectors.py`: generiert `tests/vectors/generated_vectors.json` aus `tests/vectors/test_cases.json`.
- `compare_vectors.py`: vergleicht Vulkan‑Output mit Referenzvektoren.

## Build (OpenCL + Vulkan) – Einzeiler

```bash
cc -O2 -I../../include -I../../src -o refvec_cli refvec_cli.c ../../src/mycelia_core.c ../../src/CipherCore_NoiseCtrl.c -lOpenCL -lm
```

```bash
g++ -O2 -std=c++17 -I../../android/app/src/main/cpp -I../../include -o vulkan_runner vulkan_runner.cpp ../../android/app/src/main/cpp/mycelia_vulkan_compute.cpp -lvulkan
```

## Shader Build (SPIR-V) – Einzeiler

```bash
glslangValidator -V ../../android/app/src/main/shaders/subqg_init.comp -o shaders/subqg_init.spv && glslangValidator -V ../../android/app/src/main/shaders/subqg_simulation.comp -o shaders/subqg_simulation.spv && glslangValidator -V ../../android/app/src/main/shaders/mycelia_keystream_xor.comp -o shaders/mycelia_keystream_xor.spv
```

## Vektoren erzeugen – Einzeiler

```bash
python3 gen_vectors.py
```

## Vergleich Vulkan vs OpenCL – Einzeiler

```bash
python3 compare_vectors.py ./shaders
```
