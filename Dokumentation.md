# Dokumentation

## 1. Einleitung & Scope

### 1.1 Terminologie
Um Missverständnisse zu vermeiden, werden folgende Begriffe konsistent verwendet:
- **Mycelia App:** Die Android‑Applikation (UI, Datenhaltung, Business‑Logic).
- **Mycelia Core (Native):** Die C++/Vulkan‑Bibliothek (`libmycelia_native.so`) für GPU‑beschleunigte Kryptografie.
- **Relay‑Server:** Der Python‑TCP‑Server als reines Transport‑Relay ohne Klartextkenntnis.
- **Seed:** 32‑Byte Geheimnis zur Identitäts‑/Sitzungsableitung.
- **Session:** Ephemere Verbindung pro Chat, abgeleitet aus Seed und X25519‑Handshake.

### 1.2 Status & Scope
Diese Software ist **PoC/Referenzimplementierung** für den Mycelia‑Stack. Sie demonstriert den vollständigen End‑to‑End‑Pfad (Android → JNI → Vulkan → TCP‑Relay) und die praktischen Workflows (Invite/QR, Chat, Persistenz).

**Implementiert:**
- Ende‑zu‑Ende‑Verschlüsselung (AEAD ChaCha20‑Poly1305)
- X25519‑Key‑Exchange und HKDF‑Ableitung
- GPU‑Keystream (Vulkan) als Primitive
- SubQG‑basierte Entropie‑Anreicherung (CPU‑Simulation)
- TCP‑Framing + Reconnect‑Logik

**Nicht implementiert (für Produktion erforderlich):**
- Verschlüsselung der lokalen Datenbank (Seeds liegen Base64‑kodiert im App‑Storage)
- Certificate Pinning / TLS zum Relay‑Server
- Hardware‑backed Keystore‑Integration

**Hinweis:** Für produktiven Einsatz sind Hardening‑Maßnahmen notwendig (siehe Abschnitt 11).

### 1.3 Zweck der App
Die App zeigt, wie moderne Kryptografie (ChaCha20‑Poly1305, X25519) ohne zentrale Auth‑Infrastruktur auf Android betrieben werden kann. Der Server ist bewusst „dumm“ und dient ausschließlich dem Transport.

## 2. Projekt‑ & Architekturübersicht

### Modulstruktur
Das Projekt ist monolithisch (`com.android.application`), aber logisch geschichtet:
1. **UI‑Layer (Kotlin/Compose):** MVVM‑Pattern, StateFlow‑basierte Zustände.
2. **Domain/Data‑Layer:** `ChatRepository` als Datenzugriff, `TcpChatClient` für Networking.
3. **Crypto‑Layer:**
   - **High‑Level:** `CryptoEngine` als Fassade
   - **CPU‑Pfad:** AEAD via BouncyCastle
   - **Native‑Pfad:** Vulkan‑Compute für Keystream‑XOR

### Datenfluss
1. **Input:** Nutzer‑Eingabe oder QR‑Scan.
2. **Krypto:**
   - Seed‑basierter Auth‑Handshake (HMAC)
   - X25519‑ECDH → HKDF → Session‑Key
   - AEAD‑Verschlüsselung für Nachrichten
3. **Transport:** JSON‑Payloads über TCP mit Length‑Prefix‑Framing.
4. **Storage:** Room‑DB (SQLite) für Verlauf/Counter.

## 3. Android‑Architektur

### Threading & Concurrency
- **UI:** Main‑Thread (Compose)
- **I/O:** `Dispatchers.IO` für DB + Sockets
- **Compute:** `Dispatchers.Default` für SubQG‑Simulation
- **Lifecycle:** `TcpChatClient` wird über `viewModelScope` verwaltet

## 4. Native Integration (JNI & Vulkan)

### JNI‑Bridge
- Datei: `android/app/src/main/cpp/mycelia_jni.cpp`
- Übergabe: `jbyteArray` → `std::vector<uint8_t>` → `VkBuffer`
- Ressourcenkontrolle über `nativeRelease()`

### Vulkan Compute Engine
- Shader: `mycelia_keystream_xor.comp`
- Implementiert ChaCha20‑Blockfunktion (RFC‑kompatible Konstanten und Rounds)
- Erzeugt Keystream + XOR mit Input‑Buffer
- **Kein Poly1305 im Shader** → AEAD erfolgt aktuell im CPU‑Pfad

## 5. Kryptografisches Konzept

### Primitiven
- **AEAD:** ChaCha20‑Poly1305 (BouncyCastle)
- **Stream‑Primitive:** ChaCha20 Keystream (Vulkan)
- **Key Exchange:** X25519
- **KDF:** HKDF‑SHA256

### Identität & Session
- **Seed:** 32‑Byte PSK als Auth‑Anker
- **Handshake:** HMAC über Public‑Key (Hello‑Frame)
- **Session‑Key:** ECDH‑Secret + Seed → HKDF

## 6. Netzwerkprotokoll

### Transport
- TCP‑Socket, `TCP_NODELAY`
- Reconnect mit Backoff

### Framing
- 4‑Byte Length‑Prefix (u32 Big‑Endian)

### Payload‑Typen (JSON)
- **join**: Raumbeitritt (roomId)
- **hello**: Authentifizierter Public‑Key‑Austausch
- **message**: `bodyCipherBase64`, `counter`

### Replay‑Schutz
- Monotoner Counter pro Chat
- Nachrichten mit `counter <= lastCounter` werden verworfen

## 7. Sicherheitsmodell

### Annahmen
- Seed wird out‑of‑band sicher übertragen (QR/Invite)
- Endgerät ist nicht kompromittiert

### Grenzen
- Server sieht Metadaten (IP/Timing/Größe)
- Seeds liegen unverschlüsselt im App‑Storage

## 8. App‑Benutzung

1. **Server konfigurieren** (Host/Port)
2. **Chat erstellen oder beitreten**
3. **Invite/QR teilen**
4. **Nachrichten senden/empfangen**

### Typische Fehlermeldungen
- **Connection failed:** Host/Port nicht erreichbar
- **Schlüsselaustausch ausstehend:** Gegenstelle nicht verbunden / alte App‑Version

## 9. Build & Deployment

### Voraussetzungen
- Android Studio
- NDK + CMake
- Vulkan‑fähiges Gerät empfohlen

### Build
```bash
./gradlew :app:assembleDebug
```

### Shader‑Kompilierung
```bash
./gradlew :app:compileMyceliaShaders
```

## 10. Troubleshooting

- **Vulkan‑Fehler:** Emulator muss GPU‑Pass‑Through unterstützen
- **AEAD‑Fehler:** Counter‑Desync oder falscher Seed
- **Key‑Exchange hängt:** Beide Geräte müssen aktuelle App‑Version nutzen

## 11. Sicherheitshinweise (Hardening)

1. **DB‑Verschlüsselung:** SQLCipher oder EncryptedRoom
2. **Keystore:** Seed verschlüsselt im Android‑Keystore speichern
3. **TLS + Pinning:** Transportmetadaten schützen
4. **Shader‑KATs:** Known‑Answer‑Tests bei App‑Start
