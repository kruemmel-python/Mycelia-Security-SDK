Hier ist die professionelle **`README.md`** für den `samples/web_integration/` Ordner, komplett auf Deutsch.

Sie erklärt Entwicklern und Service-Technikern präzise, wie die beiden Varianten (Pure Python vs. PHP-Microservice) funktionieren und wie man sie in Betrieb nimmt.

---

# Mycelia Web Integration Beispiele

Dieser Ordner enthält Referenz-Implementierungen zur Integration von **Mycelia Security** in Webanwendungen. Er demonstriert die **Zero-Knowledge Datenbank-Architektur**, bei der sensible Benutzerdaten durch GPU-Chaos verschlüsselt werden, bevor sie jemals die Festplatte der Datenbank berühren.

## 📂 Dateiübersicht

### Kern-Komponenten
*   `CC_OpenCl.dll`: Der Hochleistungs-GPU-Treiber (C++ Core).
*   `mycelia_chat_engine.py`: Der Python-Wrapper, der die VRAM-Interaktion und die Streaming-Verschlüsselung steuert.

### Option A: Reiner Python Server (Flask + SQLite/MySQL)
*   `mycelia_web_server.py`: Ein eigenständiger Webserver mit **SQLite**. Keine externe Datenbank erforderlich (Out-of-the-box).
*   `mycelia_web_mysql.py`: Ein eigenständiger Webserver mit **MySQL**. Direkte Verbindung von Python zur DB.
*   `mycelia_secure.db`: Die SQLite-Datenbankdatei (wird automatisch generiert).

### Option B: Enterprise PHP Architektur (Microservice)
*   `db_proxy.py`: Ein leichtgewichtiger Python-Microservice, der die GPU-Engine über eine lokale API (Port 9999) verfügbar macht.
*   `api.php`: PHP-Hilfsfunktion zur Kommunikation mit dem Python-Proxy.
*   `index.php`: Login & Registrierung (Frontend).
*   `profile.php`: Sichere Datenansicht (Entschlüsselung in Echtzeit).
*   `setup_db.php`: Ein-Klick-Setup-Skript für MySQL-Tabellen.

---

## 🏗️ Architektur

In diesem Modell kann selbst der Datenbank-Administrator **die Daten nicht lesen**.

1.  **Frontend:** Sammelt Daten im Klartext.
2.  **Middleware (Mycelia):** Sendet Daten an die GPU $\rightarrow$ Erhält `Seed` + `VerschlüsseltenBlob` zurück.
3.  **Datenbank:** Speichert nur `Seed` + `Blob`.
4.  **Abruf:** Datenbank sendet `Seed` + `Blob` an die GPU $\rightarrow$ GPU rekonstruiert das Chaos $\rightarrow$ Gibt Klartext zurück.

---

## 🚀 Schnellstart: Option A (Standalone Python)

Der einfachste Weg, die Technologie zu testen, ohne Apache/PHP installieren zu müssen.

**Voraussetzungen:**
```bash
pip install flask mysql-connector-python
```

**Server starten:**
```bash
python mycelia_web_server.py
```
*   Browser öffnen: `http://127.0.0.1:5000`
*   Daten werden lokal in `mycelia_secure.db` gespeichert.

---

## 🏢 Schnellstart: Option B (PHP Enterprise Integration)

Dies simuliert ein reales Szenario, in dem Mycelia als Sicherheits-Sidecar zu einer bestehenden PHP-Anwendung (z.B. WordPress, Magento, Custom Apps) hinzugefügt wird.

**Voraussetzungen:**
*   Ein laufender Webserver mit PHP (z.B. XAMPP, Apache, Nginx).
*   Eine laufende MySQL Datenbank.

### 1. Den Crypto-Proxy starten
Das PHP-Skript kann nicht direkt auf die GPU zugreifen. Es kommuniziert mit diesem Python-Service.
```bash
python db_proxy.py
# Fenster offen lassen! Lauscht auf Port 9999.
```

### 2. Datenbank konfigurieren
*   Bearbeiten Sie `setup_db.php` und `mycelia_web_mysql.py`, um Ihre MySQL-Zugangsdaten einzutragen (User/Passwort).
*   Verschieben Sie die `.php` Dateien in Ihr Webroot (z.B. `htdocs/mycelia/`).
*   Öffnen Sie `http://localhost/mycelia/setup_db.php`, um die Tabellen zu erstellen.

### 3. Anwendung nutzen
*   Öffnen Sie `http://localhost/mycelia/index.php`.
*   Registrieren Sie einen Benutzer.
*   **Prüfung:** Schauen Sie in Ihr MySQL-Tool (Workbench/phpMyAdmin). Sie werden nur verschlüsseltes Rauschen sehen.
*   Loggen Sie sich in `profile.php` ein: Sie sehen die entschlüsselten Daten.

---

## 🔒 Sicherheitshinweise

*   **APP_SECRET:** Die Python-Skripte enthalten eine Variable `APP_SECRET`. In einer Produktionsumgebung maskiert dies die biologischen Seeds. Es muss sicher und getrennt von der Datenbank aufbewahrt werden.
*   **Dual Dependency:** Ein Angreifer benötigt die **Datenbank** UND die **Mycelia Engine (DLL)** UND das **App Secret**, um Daten zu entschlüsseln. Fehlt nur eine Komponente, sind die Daten nutzlos.

---

**Mycelia Security SDK**  
*Next Gen GPU-Based Cryptography*