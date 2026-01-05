# TLS‑Anleitung für Mycelia (Server + App)

Diese Anleitung beschreibt, wie du TLS für den Mycelia‑Relay‑Server aktivierst, den SHA‑256‑Fingerprint ermittelst (Windows & Linux) und den Fingerprint in der App korrekt verwendest. Zusätzlich wird erklärt, wie du eine eigene CA erstellst, damit Android die Zertifikatskette akzeptiert (empfohlen für LAN‑Setups).

## 1) Voraussetzungen

- OpenSSL installiert (Windows: z. B. Git‑Bash/Win64 OpenSSL, Linux: `openssl` Paket)
- Zugriff auf den Server‑Host (IP/Hostname bekannt)
- Aktuelle App‑Version installiert

## 2) Empfohlener TLS‑Weg (eigene CA)

**Warum?** Android vertraut selbstsignierten Zertifikaten nicht automatisch. Mit einer eigenen CA bekommst du eine saubere Trust‑Chain und kannst den Leaf‑Pin zusätzlich verwenden.

### 2.1 CA + Server‑Zertifikat erzeugen (Windows CMD)

```cmd
mkdir C:\mycelia_tls
cd /d C:\mycelia_tls

:: CA Key + CA Cert
openssl genrsa -out mycelia_ca.key 4096
openssl req -x509 -new -nodes -key mycelia_ca.key -sha256 -days 3650 -out mycelia_ca.crt -subj "/CN=Mycelia-Local-CA"

:: Server Key + CSR
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr -subj "/CN=192.168.178.62"

:: SAN Datei (IP‑SAN Pflicht!)
> san.cnf echo subjectAltName = IP:192.168.178.62

:: CSR mit CA signieren
openssl x509 -req -in server.csr -CA mycelia_ca.crt -CAkey mycelia_ca.key -CAcreateserial -out server.crt -days 825 -sha256 -extfile san.cnf
```

### 2.2 CA + Server‑Zertifikat erzeugen (Linux/macOS)

```bash
mkdir -p ~/mycelia_tls
cd ~/mycelia_tls

# CA Key + CA Cert
openssl genrsa -out mycelia_ca.key 4096
openssl req -x509 -new -nodes -key mycelia_ca.key -sha256 -days 3650 -out mycelia_ca.crt -subj "/CN=Mycelia-Local-CA"

# Server Key + CSR
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr -subj "/CN=192.168.178.62"

# SAN Datei (IP‑SAN Pflicht!)
printf "subjectAltName = IP:192.168.178.62" > san.cnf

# CSR mit CA signieren
openssl x509 -req -in server.csr -CA mycelia_ca.crt -CAkey mycelia_ca.key -CAcreateserial -out server.crt -days 825 -sha256 -extfile san.cnf
```

### 2.3 Server mit TLS starten

```bash
py -m tools.server.mycelia_chat_server --tls-cert C:\mycelia_tls\server.crt --tls-key C:\mycelia_tls\server.key
```

Linux:
```bash
python3 -m tools.server.mycelia_chat_server --tls-cert ~/mycelia_tls/server.crt --tls-key ~/mycelia_tls/server.key
```

## 3) Fingerprint ermitteln (SHA‑256)

### 3.1 Windows (CMD)
```cmd
cd /d C:\mycelia_tls
openssl x509 -in server.crt -noout -fingerprint -sha256
```
Beispiel‑Ausgabe:
```
sha256 Fingerprint=E8:CC:28:...:7D:24:6B
```

### 3.2 Linux/macOS
```bash
cd ~/mycelia_tls
openssl x509 -in server.crt -noout -fingerprint -sha256
```

**Wichtig:** Der Fingerprint ist hexadezimal. Doppelpunkte sind erlaubt – die App entfernt sie automatisch.

## 4) CA in der App hinterlegen (empfohlen)

Damit Android die Kette akzeptiert, trage die CA‑PEM in der App ein:

1. Öffne **Einstellungen**
2. **TLS aktivieren** einschalten
3. **TLS CA PEM** Feld füllen (Inhalt von `mycelia_ca.crt`)
4. **TLS Pin (SHA‑256)** setzen (Fingerprint aus Abschnitt 3)

Beispiel für **TLS CA PEM** (kompletter Inhalt der Datei `mycelia_ca.crt`):
```
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
```

## 5) Was muss die App erhalten?

Die App benötigt **zwei Dinge**, um TLS sicher zu nutzen:

1. **CA‑PEM** (Trust Anchor):
   - Sorgt dafür, dass Android das Zertifikat akzeptiert.
2. **SHA‑256‑Fingerprint** (Pin):
   - Zusätzlicher Schutz gegen falsche Zertifikate.

Ohne CA‑PEM schlägt TLS bei selbstsignierten Zertifikaten fehl (`Trust anchor not found`).

## 6) Häufige Fehler

### „Trust anchor for certification path not found“
Ursache: CA nicht hinterlegt.
Lösung: `mycelia_ca.crt` in **TLS CA PEM** einfügen.

### „TLS pin mismatch“
Ursache: Fingerprint falsch kopiert oder falsches Zertifikat.
Lösung: Fingerprint neu mit `openssl x509` ermitteln und korrekt eintragen.

### Verbindung schlägt trotz TLS fehl
- IP/Port prüfen
- Firewall prüfen
- TLS aktiviert? (Server + App)

## 7) Minimal‑Setup (nur Pin, ohne CA)

Nicht empfohlen. Ohne CA‑PEM kann Android das Zertifikat nicht validieren. Für private LAN‑Server ist der CA‑Weg der saubere Standard.

---

**Kurzfassung:**
1. Eigene CA + Server‑Cert mit IP‑SAN erstellen
2. Server mit `--tls-cert/--tls-key` starten
3. Fingerprint mit `openssl x509 -fingerprint -sha256` ermitteln
4. In der App **TLS aktivieren**, **CA‑PEM** und **Pin** setzen
