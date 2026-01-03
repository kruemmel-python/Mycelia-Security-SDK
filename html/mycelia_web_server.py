import sqlite3
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
app = Flask(__name__)
app.secret_key = "WebSessionSecretKey"
DB_NAME = "mycelia_secure.db"
APP_SECRET = "MeinSuperGeheimesServerPasswort2025" 

# --- 1. Engine Initialisieren ---
print("[Server] Initialisiere Mycelia GPU Engine...")
try:
    engine = MyceliaChatEngine(0)
    engine.set_password(APP_SECRET)
except Exception as e:
    print(f"[Error] GPU Init fehlgeschlagen: {e}")
    exit(1)

# --- 2. Datenbank Setup (SQLite) ---
def init_db():
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            mycelia_seed INTEGER NOT NULL,
            encrypted_blob TEXT NOT NULL
        )
    ''')
    conn.commit()
    conn.close()

init_db()

# --- 3. Krypto-Helper (ANGEPASST AN V4 ENGINE) ---
def secure_store_data(data_dict):
    """Nimmt ein Dictionary, macht JSON, verschlüsselt es auf der GPU."""
    # 1. JSON String erzeugen
    plaintext = json.dumps(data_dict)
    
    # 2. String zu Bytes konvertieren (UTF-8)
    # WICHTIG: Die neue Engine erwartet Bytes!
    payload_bytes = plaintext.encode('utf-8')
    
    # 3. GPU Encryption (encrypt_bytes statt encrypt_text)
    packet = engine.encrypt_bytes(payload_bytes)
    
    # Packet zerlegen: [Seed 8B] [Rest...]
    seed_int = struct.unpack("Q", packet[:8])[0]
    cipher_part = packet[8:] # Der verschlüsselte Rest
    
    # Blob für DB (Base64 damit es text-safe ist)
    blob_b64 = base64.b64encode(cipher_part).decode('utf-8')
    
    return seed_int, blob_b64

def secure_retrieve_data(seed_int, blob_b64):
    """Holt Blob aus DB, rekonstruiert Paket, entschlüsselt auf GPU."""
    try:
        cipher_part = base64.b64decode(blob_b64)
        
        # Paket rekonstruieren
        packet = struct.pack("Q", seed_int) + cipher_part
        
        # GPU Decryption (decrypt_packet_to_bytes statt decrypt_packet)
        decrypted_bytes = engine.decrypt_packet_to_bytes(packet)
        
        if decrypted_bytes is None:
            return None
            
        # Bytes zurück zu String konvertieren
        json_str = decrypted_bytes.decode('utf-8')
        
        return json.loads(json_str)
    except Exception as e:
        print(f"Decryption Error: {e}")
        return None

# --- 4. HTML Templates (Identisch) ---
HTML_LOGIN = """
<!DOCTYPE html>
<html lang="de">
<head>
    <title>Mycelia Secure DB</title>
    <style>
        body { background: #121212; color: #00ff99; font-family: monospace; text-align: center; margin-top: 50px; }
        .box { border: 1px solid #333; display: inline-block; padding: 20px; margin: 10px; width: 300px; vertical-align: top; background: #1e1e1e; }
        input { background: #333; border: 1px solid #555; color: white; padding: 8px; margin: 5px; width: 90%; }
        button { background: #00ff99; color: black; border: none; padding: 10px 20px; font-weight: bold; cursor: pointer; width: 100%; }
        button:hover { background: #00cc7a; }
        h2 { border-bottom: 1px solid #333; padding-bottom: 10px; }
        .alert { color: #ff5555; }
    </style>
</head>
<body>
    <h1>MYCELIA ZERO-KNOWLEDGE DATABASE</h1>
    {% if msg %}<p class="alert">{{ msg }}</p>{% endif %}

    <div class="box">
        <h2>Login</h2>
        <form method="post" action="/login">
            <input type="text" name="user" placeholder="Username" required><br>
            <input type="password" name="pass" placeholder="Passwort" required><br>
            <button type="submit">ZUGRIFF ERBITTEN</button>
        </form>
    </div>

    <div class="box">
        <h2>Registrierung</h2>
        <form method="post" action="/register">
            <input type="text" name="user" placeholder="Username" required><br>
            <input type="password" name="pass" placeholder="Passwort" required><br>
            <hr style="border: 0; border-top: 1px solid #333;">
            <input type="text" name="vorname" placeholder="Vorname"><br>
            <input type="text" name="nachname" placeholder="Nachname"><br>
            <input type="text" name="strasse" placeholder="Straße"><br>
            <input type="text" name="hnr" placeholder="Nr."><br>
            <input type="text" name="plz" placeholder="PLZ"><br>
            <input type="text" name="ort" placeholder="Ort"><br>
            <input type="email" name="email" placeholder="E-Mail"><br>
            <button type="submit">VERSCHLÜSSELT SPEICHERN</button>
        </form>
    </div>
</body>
</html>
"""

HTML_PROFILE = """
<!DOCTYPE html>
<html lang="de">
<head>
    <style>
        body { background: #121212; color: #e0e0e0; font-family: monospace; padding: 50px; }
        .raw { background: #000; color: #555; padding: 15px; border: 1px dashed #333; margin-bottom: 30px; font-size: 11px; word-break: break-all; }
        input { background: #222; border: 1px solid #444; color: white; padding: 8px; width: 300px; margin-bottom: 10px; }
        button { background: #00ff99; color: black; border: none; padding: 10px 20px; font-weight: bold; cursor: pointer; }
        a { color: #00ff99; margin-left: 20px; }
    </style>
</head>
<body>
    <h1>Willkommen, {{ username }}</h1>
    {% if msg %}<p style="color:#00ff99">{{ msg }}</p>{% endif %}

    <h3>Was die Datenbank sieht (Encrypted Blob):</h3>
    <div class="raw">
        <strong>SEED:</strong> {{ seed }}<br>
        <strong>DATA:</strong> {{ blob }}
    </div>

    <h3>Was du siehst (GPU Decrypted):</h3>
    <form method="post" action="/update">
        Vorname: <input type="text" name="vorname" value="{{ data.vorname }}"><br>
        Nachname: <input type="text" name="nachname" value="{{ data.nachname }}"><br>
        Straße: <input type="text" name="strasse" value="{{ data.strasse }}"><br>
        Hausnr: <input type="text" name="hnr" value="{{ data.hnr }}"><br>
        PLZ: <input type="text" name="plz" value="{{ data.plz }}"><br>
        Ort: <input type="text" name="ort" value="{{ data.ort }}"><br>
        Email: <input type="email" name="email" value="{{ data.email }}"><br>
        <br>
        <button type="submit">Update Secure Profile</button>
        <a href="/logout">Logout</a>
    </form>
</body>
</html>
"""

# --- 5. Web Routes ---

@app.route("/", methods=['GET'])
def index():
    return render_template_string(HTML_LOGIN)

@app.route("/register", methods=['POST'])
def register():
    user = request.form['user']
    pw = request.form['pass']
    
    # Hash password
    pw_hash = hashlib.sha256(pw.encode()).hexdigest()
    
    user_data = {
        'vorname': request.form['vorname'],
        'nachname': request.form['nachname'],
        'strasse': request.form['strasse'],
        'hnr': request.form['hnr'],
        'plz': request.form['plz'],
        'ort': request.form['ort'],
        'email': request.form['email']
    }
    
    # Mycelia Encrypt
    seed, blob = secure_store_data(user_data)
    
    try:
        conn = sqlite3.connect(DB_NAME)
        c = conn.cursor()
        c.execute("INSERT INTO users (username, password_hash, mycelia_seed, encrypted_blob) VALUES (?, ?, ?, ?)",
                  (user, pw_hash, seed, blob))
        conn.commit()
        conn.close()
        return render_template_string(HTML_LOGIN, msg="Registrierung erfolgreich. Bitte einloggen.")
    except sqlite3.IntegrityError:
        return render_template_string(HTML_LOGIN, msg="Username existiert bereits.")

@app.route("/login", methods=['POST'])
def login():
    user = request.form['user']
    pw = request.form['pass']
    pw_hash = hashlib.sha256(pw.encode()).hexdigest()
    
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()
    c.execute("SELECT id FROM users WHERE username = ? AND password_hash = ?", (user, pw_hash))
    row = c.fetchone()
    conn.close()
    
    if row:
        session['user_id'] = row[0]
        return redirect(url_for('profile'))
    else:
        return render_template_string(HTML_LOGIN, msg="Falsche Zugangsdaten.")

@app.route("/profile")
def profile():
    if 'user_id' not in session: return redirect("/")
    
    conn = sqlite3.connect(DB_NAME)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()
    c.execute("SELECT username, mycelia_seed, encrypted_blob FROM users WHERE id = ?", (session['user_id'],))
    row = c.fetchone()
    conn.close()
    
    seed = int(row['mycelia_seed'])
    blob = row['encrypted_blob']
    
    data = secure_retrieve_data(seed, blob)
    
    if data is None:
        return "<h1 style='color:red; background:black; padding:20px;'>CRITICAL INTEGRITY ERROR: Decryption failed.</h1>"
        
    return render_template_string(HTML_PROFILE, 
                                  username=row['username'], 
                                  data=data, 
                                  seed=seed, 
                                  blob=blob[:100] + "...")

@app.route("/update", methods=['POST'])
def update():
    if 'user_id' not in session: return redirect("/")
    
    user_data = {
        'vorname': request.form['vorname'],
        'nachname': request.form['nachname'],
        'strasse': request.form['strasse'],
        'hnr': request.form['hnr'],
        'plz': request.form['plz'],
        'ort': request.form['ort'],
        'email': request.form['email']
    }
    
    seed, blob = secure_store_data(user_data)
    
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()
    c.execute("UPDATE users SET mycelia_seed = ?, encrypted_blob = ? WHERE id = ?", (seed, blob, session['user_id']))
    conn.commit()
    conn.close()
    
    return redirect(url_for('profile'))

@app.route("/logout")
def logout():
    session.pop('user_id', None)
    return redirect("/")

if __name__ == "__main__":
    print("--- Mycelia Secure Web Server ---")
    print("Öffne: http://127.0.0.1:5000")
    app.run(debug=True, use_reloader=False)