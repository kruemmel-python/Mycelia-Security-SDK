# Mycelia Security SDK – Whitepaper

## Executive Summary
Die Mycelia Security App bietet einen Ende‑zu‑Ende verschlüsselten Chat auf Android mit GPU‑beschleunigter Kryptografie über Vulkan. Die Lösung reduziert den Vertrauensumfang des Servers auf ein reines Relay und kombiniert deterministische Seed‑Modelle mit strikt kontrollierten Counter‑Offsets. Dadurch entsteht ein System, das für sicherheitskritische Kommunikation ausgelegt ist, ohne klassische Server‑Abhängigkeit für Vertraulichkeit.

Relevanz:
- Schutz sensibler Kommunikation in Unternehmen und kritischen Umgebungen
- Minimiertes Risiko durch Zero‑Knowledge‑Server
- Performancevorteile durch GPU‑Compute

## Problemstellung

### Schwächen klassischer Chat‑ und Security‑Lösungen
- Zentralisierte Server sehen Metadaten und oft Schlüsselableitungen
- Vertrauen in TLS‑Terminationspunkte
- Komplexe Schlüsselverwaltung und Recovery‑Mechanismen

### Risiken CPU‑basierter Kryptografie
- Angriffsflächen durch Shared CPU‑Ressourcen
- Timing‑Analysen und Memory‑Leak‑Risiken
- Begrenzte Parallelität bei hoher Last

### Schlüsselmanagement als Hauptproblem
Die sichere, benutzerfreundliche Handhabung von Schlüsseln ist der Kern jeder Kommunikationssicherheit. Mycelia adressiert dies durch deterministische Seeds, klar definierte Counter‑Policy und minimalen Trust in Infrastruktur.

## Technologische Innovation

### GPU‑basierte Kryptografie
Mycelia nutzt Vulkan‑Compute zur Erzeugung des Keystreams und zur Verarbeitung von Ciphertext. GPU‑Pipelines ermöglichen konsistente Performance und Isolierung gegenüber typischen CPU‑Angriffen.

### Deterministisches Chaos / Seed‑Modelle
Die Sicherheit entsteht aus deterministischen Seeds, die lokal erzeugt, gespeichert und geteilt werden. Durch strikt monotone Counter‑Offsets wird Keystream‑Reuse verhindert.

### Native Isolation
Die Kryptografie läuft in nativen Komponenten (JNI ↔ C++ ↔ Vulkan), wodurch die Angriffsfläche der App‑Schicht reduziert wird.

## Architektur & Designprinzipien

### Zero‑Knowledge
Der Server verarbeitet ausschließlich Ciphertext. Er besitzt keine Schlüssel und kann keine Nachrichten entschlüsseln.

### Minimaler Trust
Nur die Endgeräte müssen vertrauenswürdig sein. Der Server bleibt ein Relay ohne Wissen über Inhalte.

### Ephemeral Keys
Der Ansatz erlaubt die Ableitung sitzungsbezogener Schlüssel und minimiert langfristige Schlüsselbindungen.

### Keine Server‑Abhängigkeit für Sicherheit
Die Verschlüsselung ist vollständig clientseitig. Serverlogik ist austauschbar.

## Sicherheitsanalyse

### Bedrohungsmodelle
- Netzwerkangriffe (MITM)
- Server‑Kompromittierung
- Replay‑Angriffe
- Key Extraction

### Angriffsflächen
- Endgerät (App, OS, Memory)
- Netzwerkpfad
- Server‑Relay

### Warum klassische Angriffe scheitern
- MITM: Ende‑zu‑Ende‑Verschlüsselung
- Server‑Leak: Zero‑Knowledge‑Relay
- Replay: Counter‑Policy
- Key Extraction: lokale Speicherung ohne Server‑Sync

## Vergleich mit bestehenden Lösungen

### Klassische Messenger
Diese benötigen oft serverseitige Identitätslogik, Recovery und Metadaten‑Handling. Mycelia reduziert dies auf das absolute Minimum.

### TLS‑basierte Systeme
TLS schützt Transport, nicht Ende‑zu‑Ende. Mycelia schützt Inhalte unabhängig vom Transport.

### Hardware‑Security‑Module (HSM)
HSMs bieten Hardware‑Schlüssel, sind aber teuer und infrastrukturlastig. Mycelia setzt auf Endgeräte‑Kryptografie ohne zentrale HSM‑Abhängigkeit.

## Einsatzszenarien

- **Unternehmen**: vertrauliche Kommunikation zwischen Teams
- **Journalisten**: Schutz von Quellen
- **Aktivisten**: sichere Koordination
- **KRITIS**: Kommunikation in sicherheitskritischen Infrastrukturen
- **Private Kommunikation**: Schutz vor Datenabfluss

## Grenzen & Verantwortung

### Design‑Tradeoffs
- Seed‑Sharing erfordert sicheren Kanal
- Keine serverseitige Recovery
- Sicherheit hängt von Gerät‑Integrität ab

### Rechtliche Aspekte
Je nach Einsatzland gelten regulatorische Anforderungen (z. B. Exportkontrollen, Datenschutz). Die App liefert die technische Basis, Compliance bleibt Aufgabe des Betreibers.

### Verantwortung des Nutzers
- Seeds sicher übertragen
- Geräte schützen
- Updates regelmäßig einspielen

## Zukunftsausblick

### Erweiterbarkeit
- Ausbau der GPU‑Pipeline
- Integration weiterer Plattformen (Desktop/iOS)
- Erweiterte Schlüsselmechanismen und AAD‑Metadaten

### Integration in andere Plattformen
Die modulare Architektur erlaubt den Einsatz als SDK in anderen Anwendungen.

### Langfristige Vision
Mycelia ist als Sicherheits‑Backbone für ein dezentrales Kommunikations‑Ökosystem gedacht, in dem Endgeräte die volle kryptografische Hoheit behalten.

## Fazit

Mycelia liefert eine technisch belastbare, minimal‑trust‑basierte Lösung für sichere Kommunikation. Der Einsatz von GPU‑Compute, deterministischen Seeds und Zero‑Knowledge‑Servern schafft einen klaren Sicherheitsvorteil gegenüber klassischen Systemen. Die Architektur ist auf Enterprise‑Einsatz und externe Audits ausgelegt.
