import mysql.connector
import json
import base64
import struct
import os
import hashlib
from flask import Flask, request, session, redirect, url_for, render_template_string

# --- Mycelia Engine Import ---
try:
    from mycelia_chat_engine import MyceliaChatEngine
except ImportError:
    print("CRITICAL: mycelia_chat_engine.py fehlt!")
    exit(1)

# --- Konfiguration ---
def load_dotenv(path: str) -> None:
    if not os.path.exists(path):
        return
    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key and key not in os.environ:
                os.environ[key] = value

load_dotenv(os.path.join(os.path.dirname(__file__), ".env"))

def require_env(name: str) -> str:
    value = os.getenv(name)
    if not value:
        raise RuntimeError(f"Missing required environment variable: {name}")
    return value

app = Flask(__name__)
app.secret_key = require_env("MYCELIA_FLASK_SECRET")
APP_SECRET = require_env("MYCELIA_ENGINE_PASSWORD")

# MySQL Konfiguration
db_config = {
    'user': require_env("MYCELIA_DB_USER"),
    'password': require_env("MYCELIA_DB_PASSWORD"),
    'host': require_env("MYCELIA_DB_HOST"),
    'database': require_env("MYCELIA_DB_NAME"),
    'raise_on_warnings': False
}

# --- 1. Engine Initialisieren ---
print("[Server] Initialisiere Mycelia GPU Engine...")
try:
    engine = MyceliaChatEngine(0)
    engine.set_password(APP_SECRET)
except Exception as e:
    print(f"[Error] GPU Init fehlgeschlagen: {e}")
    exit(1)

# --- 2. Datenbank Setup (MySQL) ---
def init_db():
    try:
        conn = mysql.connector.connect(user=db_config['user'], password=db_config['password'], host=db_config['host'])
        c = conn.cursor()
        c.execute("CREATE DATABASE IF NOT EXISTS mycelia_secure_db")
        conn.close()

        conn = mysql.connector.connect(**db_config)
        c = conn.cursor()
        c.execute('''
            CREATE TABLE IF NOT EXISTS users (
                id INT AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(50) NOT NULL UNIQUE,
                password_hash VARCHAR(255) NOT NULL,
                mycelia_seed BIGINT NOT NULL,
                encrypted_blob LONGTEXT NOT NULL
            )
        ''')
        conn.commit()
        conn.close()
        print("[Server] MySQL Verbindung erfolgreich.")
    except mysql.connector.Error as err:
        print(f"[Critical] MySQL Fehler: {err}")
        exit(1)

init_db()

# --- 3. Krypto-Helper (MIT MYSQL FIX) ---

def to_mysql_bigint(val):
    """Wandelt uint64 in int64 um (für MySQL BIGINT)"""
    if val > 9223372036854775807:
        return val - 18446744073709551616
    return val

def from_mysql_bigint(val):
    """Wandelt int64 zurück in uint64 (für GPU Engine)"""
    if val < 0:
        return val + 18446744073709551616
    return val

def secure_store_data(data_dict):
    plaintext = json.dumps(data_dict)
    payload_bytes = plaintext.encode('utf-8')
    
    # Verschlüsseln
    packet = engine.encrypt_bytes(payload_bytes)
    
    # Seed extrahieren (Unsigned)
    seed_uint = struct.unpack("Q", packet[:8])[0]
    
    # FIX: Für MySQL passend machen (Signed)
    seed_db = to_mysql_bigint(seed_uint)
    
    cipher_part = packet[8:]
    blob_b64 = base64.b64encode(cipher_part).decode('utf-8')
    
    return seed_db, blob_b64

def secure_retrieve_data(seed_db, blob_b64):
    try:
        cipher_part = base64.b64decode(blob_b64)
        
        # FIX: Seed zurückwandeln für GPU (Unsigned)
        seed_uint = from_mysql_bigint(seed_db)
        
        # Paket rekonstruieren
        packet = struct.pack("Q", seed_uint) + cipher_part
        
        decrypted_bytes = engine.decrypt_packet_to_bytes(packet)
        
        if decrypted_bytes is None: return None
        json_str = decrypted_bytes.decode('utf-8')
        return json.loads(json_str)
    except Exception as e:
        print(f"Decryption Error: {e}")
        return None

# --- HTML Templates ---
HTML_LOGIN = """<!DOCTYPE html><html lang="de"><head><title>Mycelia Enterprise</title><style>body { background: #121212; color: #00ff99; font-family: monospace; text-align: center; margin-top: 50px; } .box { border: 1px solid #333; display: inline-block; padding: 20px; background: #1e1e1e; } input { background: #333; border: 1px solid #555; color: white; padding: 8px; margin: 5px; } button { background: #00ff99; border: none; padding: 10px; font-weight: bold; cursor: pointer; }</style></head><body><h1>MYCELIA ENTERPRISE DB (MySQL)</h1>{% if msg %}<p style="color:red">{{ msg }}</p>{% endif %}<div class="box"><h2>Login</h2><form method="post" action="/login"><input type="text" name="user" placeholder="Username" required><br><input type="password" name="pass" placeholder="Passwort" required><br><button type="submit">LOGIN</button></form></div><div class="box"><h2>Registrierung</h2><form method="post" action="/register"><input type="text" name="user" placeholder="Username" required><br><input type="password" name="pass" placeholder="Passwort" required><br><hr><input type="text" name="vorname" placeholder="Vorname"><br><input type="text" name="nachname" placeholder="Nachname"><br><input type="email" name="email" placeholder="E-Mail"><br><button type="submit">SECURE INSERT</button></form></div></body></html>"""

HTML_PROFILE = """<!DOCTYPE html><html lang="de"><head><style>body { background: #121212; color: #e0e0e0; font-family: monospace; padding: 50px; } .raw { background: #000; color: #555; padding: 15px; border: 1px dashed #333; margin-bottom: 20px; word-break: break-all; font-size:10px; } input { background: #222; border: 1px solid #444; color: white; padding: 8px; } button { background: #00ff99; border: none; padding: 10px; font-weight: bold; cursor: pointer; }</style></head><body><h1>User: {{ username }}</h1><h3>MySQL Speicherabbild (Encrypted):</h3><div class="raw">SEED: {{ seed }}<br>BLOB: {{ blob }}</div><h3>GPU Decrypted View (VRAM):</h3><form method="post" action="/update"><input type="text" name="vorname" value="{{ data.vorname }}"><input type="text" name="nachname" value="{{ data.nachname }}"><input type="email" name="email" value="{{ data.email }}"><button type="submit">Update</button><a href="/logout" style="color:#666; margin-left:10px">Logout</a></form></body></html>"""

# --- Routes ---
@app.route("/", methods=['GET'])
def index(): return render_template_string(HTML_LOGIN)

@app.route("/register", methods=['POST'])
def register():
    user, pw = request.form['user'], request.form['pass']
    pw_hash = hashlib.sha256(pw.encode()).hexdigest()
    
    user_data = {'vorname': request.form['vorname'], 'nachname': request.form['nachname'], 'email': request.form['email']}
    seed, blob = secure_store_data(user_data)
    
    try:
        conn = mysql.connector.connect(**db_config)
        c = conn.cursor()
        c.execute("INSERT INTO users (username, password_hash, mycelia_seed, encrypted_blob) VALUES (%s, %s, %s, %s)",
                  (user, pw_hash, seed, blob))
        conn.commit()
        conn.close()
        return render_template_string(HTML_LOGIN, msg="Registrierung erfolgreich.")
    except mysql.connector.Error as err:
        return render_template_string(HTML_LOGIN, msg=f"MySQL Error: {err}")

@app.route("/login", methods=['POST'])
def login():
    user, pw = request.form['user'], request.form['pass']
    pw_hash = hashlib.sha256(pw.encode()).hexdigest()
    
    conn = mysql.connector.connect(**db_config)
    c = conn.cursor()
    c.execute("SELECT id FROM users WHERE username = %s AND password_hash = %s", (user, pw_hash))
    row = c.fetchone()
    conn.close()
    
    if row:
        session['user_id'] = row[0]
        return redirect(url_for('profile'))
    else:
        return render_template_string(HTML_LOGIN, msg="Falsche Daten.")

@app.route("/profile")
def profile():
    if 'user_id' not in session: return redirect("/")
    
    conn = mysql.connector.connect(**db_config)
    c = conn.cursor(dictionary=True)
    c.execute("SELECT username, mycelia_seed, encrypted_blob FROM users WHERE id = %s", (session['user_id'],))
    row = c.fetchone()
    conn.close()
    
    seed = int(row['mycelia_seed'])
    blob = row['encrypted_blob']
    data = secure_retrieve_data(seed, blob)
    
    if data is None: return "CRITICAL INTEGRITY ERROR"
    return render_template_string(HTML_PROFILE, username=row['username'], data=data, seed=seed, blob=blob[:100]+"...")

@app.route("/update", methods=['POST'])
def update():
    if 'user_id' not in session: return redirect("/")
    user_data = {'vorname': request.form['vorname'], 'nachname': request.form['nachname'], 'email': request.form['email']}
    seed, blob = secure_store_data(user_data)
    
    conn = mysql.connector.connect(**db_config)
    c = conn.cursor()
    c.execute("UPDATE users SET mycelia_seed = %s, encrypted_blob = %s WHERE id = %s", (seed, blob, session['user_id']))
    conn.commit()
    conn.close()
    return redirect(url_for('profile'))

@app.route("/logout")
def logout():
    session.pop('user_id', None)
    return redirect("/")

if __name__ == "__main__":
    print(f"--- Mycelia Enterprise Server (MySQL Backend) ---")
    app.run(debug=True, use_reloader=False)
