# Mycelia Security SDK – Technische & Benutzer-Dokumentation

## 1. Einleitung

### Zweck der App
Die Mycelia Security App ist ein Android‑basierter, Ende‑zu‑Ende verschlüsselter Chat‑Client, der GPU‑beschleunigte Kryptografie über den vorhandenen Vulkan‑Compute‑Pfad nutzt. Ziel ist eine sichere, performante Kommunikation auf realen Android‑Geräten (Android 10+), ohne dass der Server Zugriff auf Klartext oder Schlüssel erhält.

### Einordnung im Mycelia‑Ökosystem
Die App ist ein Frontend für den bereits bestehenden Mycelia‑Krypto‑Stack im Repository. Sie nutzt die JNI‑Bridge, die native Vulkan‑Compute‑Engine sowie das serverseitige TCP‑Relay. Die App ist damit ein konkreter, produktionsnaher Einsatz des Mycelia‑Sicherheits‑SDKs.

### Abgrenzung zu klassischen Chat‑Apps
Klassische Messenger basieren oft auf TLS‑gesicherten Transporten mit serverseitiger Sitzungslogik. Mycelia geht einen anderen Weg:
- **Zero‑Knowledge‑Server**: Der Server sieht ausschließlich Ciphertext.
- **GPU‑basierte Kryptografie**: Der Keystream wird über Vulkan erzeugt.
- **Seed‑basierte Sitzungen**: Sicherheit wird über deterministische Seeds und streng kontrollierte Counter‑Offsets gewährleistet.

## 2. Systemübersicht

### Architekturübersicht
```
Android App
  └─ UI (Jetpack Compose)
  └─ ViewModel/StateFlow
  └─ Networking (TCP + Framing)
  └─ Crypto Layer (JNI ↔ Vulkan Compute)

JNI Bridge (C++)
  └─ MyceliaNative API
  └─ MyceliaVulkanCompute

Server (Python asyncio)
  └─ TCP Relay
  └─ Rooms via roomId (Invite Code)
```

### Kommunikationsmodell
- TCP‑Verbindung zwischen App und Server
- Länge‑präfixiertes Framing (u32 Big‑Endian)
- JSON‑Payload mit `roomId`, `type`, `bodyCipherBase64`, `counter` und optionalen Hello‑Frames

### Sicherheitsmodell
- End‑to‑Ende‑Verschlüsselung
- Server ist ein reines Relay
- Schlüsselmaterial verbleibt auf den Geräten
- Counter‑basierter Keystream schützt gegen Re‑Use

## 3. Technische Architektur

### Android‑Layer
- **UI**: Jetpack Compose Screens (Conversations, Chat, Settings, Invite/QR)
- **ViewModel**: StateFlow für UI‑State, Verbindung und Nachrichten
- **Networking**: TCP‑Client mit Reconnect‑Strategie und Framing
- **Persistence**: Room DB für Conversations und Messages

### JNI‑Bridge
Die JNI‑Bridge kapselt den Zugriff auf die native Vulkan‑Compute‑Bibliothek:
- `nativeInit(shaderDir)`
- `nativeEncrypt(handle, input, seed, streamOffset)`
- `nativeDecrypt(handle, input, seed, streamOffset)`

### Native Crypto Engine (GPU / Vulkan)
- Vulkan‑Compute Pipeline
- Shader‑basierte Keystream‑Generierung
- XOR‑Operationen auf GPU

### Speicher‑ & Schlüsselhandling
- Seed wird lokal persistiert (Room)
- Session‑Counter wird persistiert
- Kein Logging sensibler Daten
- GPU‑Buffer werden nur für die Verarbeitung verwendet

## 4. Kryptografisches Design

### Seed‑basierte Schlüsselableitung
- Jeder Chat besitzt eine Session‑Seed
- Seed wird als Base64 gespeichert und geteilt
- Seed + Counter → deterministische Keystream‑Offsets

### Counter‑ & Session‑Management
- Pro Chat ein monotoner `messageCounter`
- `stream_offset = counter * STRIDE + byteOffset`
- STRIDE ist fix, um Keystream‑Reuse auszuschließen

### Warum GPU‑basierte Kryptografie
- Hohe Parallelität
- Stabile Performance bei großen Datenmengen
- Nutzung vorhandener Vulkan‑Compute‑Pipeline

### Unterschiede zu AES/RSA/Klassikern
- Kein klassisches Block‑Cipher‑Schema
- Stream‑Cipher‑Ansatz mit deterministischem Keystream
- Key/Seed‑Management strikt lokal

## 5. Netzwerkprotokoll

### TCP‑Kommunikation
- Permanente TCP‑Verbindung
- Reconnect mit Backoff
- Fehlerzustände werden sichtbar angezeigt

### Framing‑Strategie
- 4‑Byte Längenpräfix (u32 Big‑Endian)
- Payload als JSON

### Replay‑Schutz
- Counter‑Management verhindert doppelte/alte Nachrichten
- Nachrichten mit `counter <= lastCounter` werden verworfen

### Fehlerbehandlung
- Verbindungsabbrüche → Reconnect
- Falsche Längen → Abbruch
- Auth‑Fehler → Nachricht verworfen

## 6. Sicherheitskonzept

### Zero‑Knowledge‑Ansatz
- Server sieht nur verschlüsselte Payloads
- Keine Klartext‑Verarbeitung
- Keine Schlüsselhaltung auf Serverseite

### Keine persistente Schlüsselhaltung
- Seeds werden lokal gespeichert
- Keine Weitergabe an den Server

### Schutz vor Angriffsvektoren
- **Memory Dumps**: Schlüssel nicht geloggt, nur lokal
- **MITM**: Ende‑zu‑Ende‑Verschlüsselung
- **Replay**: Counter‑Prüfung
- **Key Extraction**: Keine Schlüssel im Server

## 7. App‑Bedienung

### Installation
- APK via Android Studio bauen
- Debug/Release möglich

### Erster Start
- Conversations‑Liste
- Neuer Chat erzeugt lokalen Seed

### Verbindung herstellen
- Server Host/Port in Settings setzen
- Port muss im Netzwerk erreichbar sein

### Nachrichten senden/empfangen
- Nachrichten werden verschlüsselt versendet
- Empfangene Nachrichten werden entschlüsselt angezeigt

### Typische Fehlermeldungen
- **Connection failed**: Host/Port nicht erreichbar
- **Schlüsselaustausch ausstehend**: Gegenstelle nicht verbunden

## 8. Build & Deployment

### Voraussetzungen
- Android Studio
- NDK + CMake
- Vulkan‑fähiges Gerät empfohlen

### Build
```bash
./gradlew :app:assembleDebug
```

### Native Bibliotheken
- `libmycelia_native.so` via CMake

### Debug vs. Release
- Debug: Logging aktiv
- Release: ProGuard optional

## 9. Fehlerdiagnose & Troubleshooting

### Typische Build‑Fehler
- Shader‑Compile: glslc‑Version prüfen
- NDK‑Pfad fehlt: Android Studio SDK Manager

### Laufzeitfehler
- Crash beim Start: Room‑Migration (DB löschen)
- Keine Nachrichten: Key‑Exchange/Invite prüfen

### Netzwerkprobleme
- Firewall blockiert Port
- Gerät nicht im selben WLAN

## 10. Sicherheitshinweise & Best Practices

- Nutzung in separatem Testnetz vor Produktion
- Seed‑Transfer nur über vertrauenswürdige Kanäle
- Keine Debug‑Builds in produktiven Umgebungen
- Regelmäßige Sicherheitsreviews

## 11. Lizenz & Haftungsausschluss

Dieses Projekt kann sowohl Open‑Source‑ als auch proprietär betrieben werden. Alle sicherheitsrelevanten Aussagen gelten unter der Prämisse korrekter Implementierung und sicherer Betriebsumgebung.

Haftungsausschluss: Die Nutzung erfolgt auf eigenes Risiko. Es wird keine Garantie für Fehlerfreiheit oder Eignung für spezifische Einsatzzwecke übernommen.
