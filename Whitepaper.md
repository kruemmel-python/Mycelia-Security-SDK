# Whitepaper

# Mycelia Security: Hybrid Native Android Architecture

## Executive Summary
Mycelia ist eine Android‑basierte Kommunikationsplattform, die als PoC für eine dezentrale, hardwarenahe Sicherheitsarchitektur dient. Sie reduziert Server‑Trust durch Zero‑Knowledge‑Relay und verlagert kryptografische Workloads via Vulkan Compute in eine separate Ausführungsdomäne des Endgeräts.

## 1. Problemstellung

### Vertrauensmodelle in der Kommunikation
Klassische Messenger hängen von zentralen Servern (Identitäts‑/Key‑Directory) ab. Metadaten entstehen zwangsläufig. Mycelia verschiebt das Vertrauen vollständig auf die Endgeräte und behandelt das Netzwerk als untrusted Transport.

### CPU‑Monopol und Angriffsflächen
Krypto‑Operationen konkurrieren mit UI‑Threads und können bei kompromittierten OS‑Prozessen ausgelesen werden. GPU‑Offloading reduziert diese Angriffsfläche und erhöht Parallelität.

## 2. Technologischer Ansatz

### Native Kryptografie & Vulkan Compute
- ChaCha20‑Keystream als GLSL‑Compute‑Shader
- Execution‑Domain‑Separation (GPU vs. JVM‑Heap)
- Hoher Durchsatz für Bulk‑Daten

### SubQG‑Entropie
Deterministische Simulation dynamischer Felder (CPU‑SubQG) erzeugt komplexe Muster, die mit `SecureRandom` gemischt werden. Ziel ist eine zusätzliche Entropiequelle bei potenziell schwachem OS‑RNG.

## 3. Architekturprinzipien

### Decentralized Trust
- **Identität = Wissen:** Seed ist die Identität
- **Server = Relay:** Kein Klartext, kein Key‑Material

### Ephemere Sicherheit & PFS
- X25519‑ECDH → HKDF → Session‑Keys
- Seeds dienen zur Authentifizierung, nicht als alleiniger Session‑Key

## 4. Sicherheitsanalyse

### Bedrohungsmodell
- MITM: verhindert durch AEAD/HMAC
- Server‑Kompromittierung: keine Schlüssel/Keystreams
- Replay: Counter‑Policy

### Grenzen
- Metadaten sichtbar (IP/Timing)
- Seeds lokal unverschlüsselt (PoC‑Status)

## 5. Vergleich mit bestehenden Lösungen

| Merkmal | Mycelia | Klassische Messenger | HSM‑basierte Systeme |
|---|---|---|---|
| Execution Domain | Hybrid CPU/GPU | CPU | Hardware‑isoliert |
| Trust Anchor | Lokaler Seed | Server/Account | Hardware Key |
| Server‑Wissen | Zero‑Knowledge | Metadaten/Graph | N/A |

## 6. Einsatzszenarien
- Unternehmen & kritische Infrastruktur
- Journalisten & Aktivisten
- Temporäre Hochsicherheits‑Sessions

## 7. Fazit & Ausblick

Mycelia zeigt, dass Android‑Geräte als souveräne Krypto‑Prozessoren genutzt werden können. Für einen produktiven Einsatz sind Data‑at‑Rest‑Protection, Keystore‑Integration und TLS‑Hardening zwingend erforderlich. Perspektivisch kann Poly1305 vollständig auf GPU migriert werden, um echte GPU‑AEAD‑Pipelines zu ermöglichen.
