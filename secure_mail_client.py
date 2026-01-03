import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog
import smtplib
import imaplib
import email
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.base import MIMEBase
from email import encoders
from email.utils import formataddr
import json
import threading
import os
import sys
import ctypes
import struct
import random
import hashlib
import base64
import time
import numpy as np
from concurrent.futures import ThreadPoolExecutor

# --- OAuth2 Imports ---
try:
    from google.auth.transport.requests import Request
    from google.oauth2.credentials import Credentials
    from google_auth_oauthlib.flow import InstalledAppFlow
    OAUTH_AVAILABLE = True
except ImportError:
    OAUTH_AVAILABLE = False
    print("WARNUNG: 'google-auth' Bibliotheken fehlen. Gmail OAuth wird nicht funktionieren.")
    print("Bitte installieren: pip install google-auth google-auth-oauthlib google-auth-httplib2")

# =============================================================================
# TEIL 1: MYCELIA CORE (Verschlüsselung)
# =============================================================================

def resource_path(relative_path):
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)

DLL_NAME = "./bin/CC_OpenCl.dll" if os.name == 'nt' else "./bin/CC_OpenCl.so"
LIB_PATH = resource_path(DLL_NAME)

if not os.path.exists(LIB_PATH):
    if os.path.exists(os.path.join("build", DLL_NAME)):
        LIB_PATH = os.path.join("build", DLL_NAME)
    elif os.path.exists(DLL_NAME):
        LIB_PATH = os.path.abspath(DLL_NAME)

MYCELIA_AVAILABLE = False
try:
    if os.name == 'nt':
        cl = ctypes.CDLL(LIB_PATH, winmode=0)
    else:
        cl = ctypes.CDLL(LIB_PATH)
    
    class HPIOAgent(ctypes.Structure):
        _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float), ("energy", ctypes.c_float), ("coupling", ctypes.c_float)]

    cl.initialize_gpu.argtypes = [ctypes.c_int]
    cl.subqg_set_deterministic_mode.argtypes = [ctypes.c_int, ctypes.c_ulonglong]
    cl.subqg_initialize_state.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    cl.subqg_simulation_step.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
    cl.subqg_debug_read_channel.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int]
    MYCELIA_AVAILABLE = True
    print(f"[C] Mycelia Engine geladen: {LIB_PATH}")
except Exception as e:
    print(f"[WARNUNG] Konnte DLL nicht laden: {e}")

HEADER_MAGIC = b'MYZ4' 
VERSION = 4
GRID_SIZE = 256 * 256
CHUNK_SIZE = GRID_SIZE
C_LOCK = threading.Lock()

class GPUSlot:
    def __init__(self, index):
        self.index = index
        if MYCELIA_AVAILABLE:
            cl.initialize_gpu(index)

    def generate_key_block(self, seed, block_index):
        if not MYCELIA_AVAILABLE:
            random.seed(seed + block_index)
            return bytes([random.randint(0, 255) for _ in range(GRID_SIZE)])

        with C_LOCK:
            block_seed = seed + (block_index * 7919) 
            cl.subqg_set_deterministic_mode(1, ctypes.c_ulonglong(block_seed))
            cl.subqg_initialize_state(self.index, 0.5, 0.5, 0.005, 0.5)
            cl.subqg_simulation_step(self.index, 0.5, 0.5, 0.5, None, None, None, None, None, None, None, 0)
            raw_buffer = np.zeros(GRID_SIZE, dtype=np.float32)
            cl.subqg_debug_read_channel(self.index, 0, raw_buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), GRID_SIZE)
            key_int = raw_buffer.view(np.uint32)
            key_int = (key_int ^ (key_int >> 16)) * 0x45d9f3b
            return (key_int & 0xFF).astype(np.uint8)

class MyceliaVaultV4:
    def __init__(self):
        self.gpus = []
        self.gpus.append(GPUSlot(0))

    def _process_chunk_task(self, gpu_slot, data_chunk, master_seed, block_index):
        key_bytes = gpu_slot.generate_key_block(master_seed, block_index)
        n = len(data_chunk)
        current_key = key_bytes[:n]
        np_data = np.frombuffer(data_chunk, dtype=np.uint8)
        xor_result = np.bitwise_xor(np_data, current_key)
        return xor_result.tobytes()

    def process_file(self, input_path, output_path, seed, mode='encrypt'):
        file_size = os.path.getsize(input_path)
        hasher = hashlib.blake2b(key=struct.pack("Q", seed)[:32])
        block_index = 0
        start_offset = 0
        payload_len = file_size
        
        if mode == 'encrypt':
            with open(output_path, 'wb') as f:
                fn = os.path.basename(input_path).encode('utf-8')
                header = HEADER_MAGIC + struct.pack('I', VERSION) + struct.pack('Q', seed) + struct.pack('H', len(fn)) + fn
                f.write(header)
                hasher.update(header)
        else: # decrypt
            with open(input_path, 'rb') as f:
                magic = f.read(4)
                if magic != HEADER_MAGIC: raise ValueError("Kein Mycelia Format")
                f.read(4); f.read(8) # Ver, Seed skip
                fn_len = struct.unpack('H', f.read(2))[0]
                f.read(fn_len)
                start_offset = f.tell()
                payload_len = file_size - start_offset - 64 # minus tag
            
        with open(input_path, 'rb') as fin, open(output_path, 'ab' if mode=='encrypt' else 'wb') as fout:
            fin.seek(start_offset if mode == 'decrypt' else 0)
            read_count = 0
            while read_count < payload_len:
                chunk_sz = min(CHUNK_SIZE, payload_len - read_count)
                if chunk_sz <= 0: break
                chunk = fin.read(chunk_sz)
                if not chunk: break
                if mode == 'decrypt': hasher.update(chunk)
                
                res = self._process_chunk_task(self.gpus[0], chunk, seed, block_index)
                block_index += 1
                fout.write(res)
                read_count += len(res)
                if mode == 'encrypt': hasher.update(res)
            
            if mode == 'encrypt':
                fout.write(hasher.digest())

# =============================================================================
# TEIL 2: OAUTH2 MANAGER (Google Logic)
# =============================================================================

SCOPES = ['https://mail.google.com/']

class OAuthManager:
    @staticmethod
    def get_token(email):
        """Holt einen gültigen Token. Öffnet Browser wenn nötig."""
        if not OAUTH_AVAILABLE:
            raise Exception("Google Auth Libs nicht installiert.")
            
        creds = None
        token_file = f'token_{email}.json'
        
        # 1. Existierenden Token laden
        if os.path.exists(token_file):
            try:
                creds = Credentials.from_authorized_user_file(token_file, SCOPES)
            except:
                os.remove(token_file) # Korrupten Token löschen

        # 2. Wenn kein Token oder abgelaufen -> Neu anmelden
        if not creds or not creds.valid:
            if creds and creds.expired and creds.refresh_token:
                try:
                    creds.refresh(Request())
                except:
                    creds = None # Refresh failed, full login needed
            
            if not creds:
                if not os.path.exists('credentials.json'):
                    raise FileNotFoundError("DATEI FEHLT: 'credentials.json' muss im Ordner liegen!")
                
                flow = InstalledAppFlow.from_client_secrets_file('credentials.json', SCOPES)
                creds = flow.run_local_server(port=0)
            
            # Token speichern
            with open(token_file, 'w') as token:
                token.write(creds.to_json())
        
        return creds.token

    @staticmethod
    def build_xoauth2_string(username, access_token, base64_encode=True):
        """
        Erstellt den Auth-String.
        Fix: Erlaubt Rückgabe als Bytes (für imaplib) oder Base64-String (für smtplib).
        """
        auth_string = f"user={username}\1auth=Bearer {access_token}\1\1"
        if base64_encode:
            return base64.b64encode(auth_string.encode("utf-8")).decode("utf-8")
        return auth_string.encode("utf-8") 

# =============================================================================
# TEIL 3: ACCOUNT & GUI
# =============================================================================

CONFIG_FILE = "mail_accounts.json"

class AccountManager:
    def __init__(self):
        self.accounts = self.load_accounts()

    def load_accounts(self):
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, 'r') as f: return json.load(f)
            except: return []
        return []

    def save_accounts(self):
        with open(CONFIG_FILE, 'w') as f: json.dump(self.accounts, f, indent=4)

    def add_account(self, data):
        self.accounts.append(data)
        self.save_accounts()

class SecureMailClient:
    def __init__(self, root):
        self.root = root
        self.root.title("Mycelia Secure Mail Client (OAuth2 Supported)")
        self.root.geometry("1100x700")
        
        self.acc_manager = AccountManager()
        self.vault = MyceliaVaultV4()
        self.fetched_messages = []
        
        self.setup_ui()

    def setup_ui(self):
        style = ttk.Style()
        style.theme_use('clam')
        
        toolbar = ttk.Frame(self.root, padding=5)
        toolbar.pack(side=tk.TOP, fill=tk.X)
        
        ttk.Button(toolbar, text="Einstellungen / Konten", command=self.open_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(toolbar, text="Abrufen (IMAP)", command=self.fetch_mails).pack(side=tk.LEFT, padx=5)
        ttk.Button(toolbar, text="Neue Mail Verfassen", command=self.open_compose).pack(side=tk.LEFT, padx=5)
        ttk.Button(toolbar, text="Datei entschlüsseln (Manuell)", command=self.decrypt_local_file).pack(side=tk.LEFT, padx=20)
        
        self.current_account_var = tk.StringVar()
        self.acc_combo = ttk.Combobox(toolbar, textvariable=self.current_account_var, state="readonly", width=30)
        self.acc_combo.pack(side=tk.RIGHT, padx=5)
        self.update_account_combo()
        
        paned = ttk.PanedWindow(self.root, orient=tk.VERTICAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.tree = ttk.Treeview(paned, columns=("From", "Subject", "Date"), show="headings")
        self.tree.heading("From", text="Von")
        self.tree.heading("Subject", text="Betreff")
        self.tree.heading("Date", text="Datum")
        self.tree.bind("<Double-1>", self.on_mail_double_click)
        paned.add(self.tree, weight=1)
        
        self.status_var = tk.StringVar(value="Bereit. (Für Gmail OAuth: credentials.json benötigt)")
        statusbar = ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W)
        statusbar.pack(side=tk.BOTTOM, fill=tk.X)

    def update_account_combo(self):
        # Fix: Nutzt .get() mit Default-Wert, um Absturz bei alten Configs zu verhindern
        names = [f"{acc['name']} ({acc.get('auth_type', 'password')})" for acc in self.acc_manager.accounts]
        self.acc_combo['values'] = names
        if names: self.acc_combo.current(0)
        else: self.acc_combo.set("Keine Konten")

    def get_current_account(self):
        if not self.acc_combo.get(): return None
        idx = self.acc_combo.current()
        if idx >= 0 and idx < len(self.acc_manager.accounts):
            acc = self.acc_manager.accounts[idx]
            # Sicherstellen dass auth_type existiert (Migration)
            if 'auth_type' not in acc: acc['auth_type'] = 'password'
            return acc
        return None

    # --- Manual Decrypt ---
    def decrypt_local_file(self):
        path = filedialog.askopenfilename(title="Wähle eine .myc Datei", filetypes=[("Mycelia Encrypted", "*.myc"), ("Alle Dateien", "*.*")])
        if not path: return
            
        seed_str = simpledialog.askstring("Mycelia Security", "Bitte den Seed (Schlüssel) eingeben:")
        if not seed_str or not seed_str.isdigit():
            messagebox.showerror("Fehler", "Ungültiger Seed.")
            return
            
        seed = int(seed_str)
        if path.endswith(".myc"): out_path = path[:-4]
        else: out_path = path + ".decrypted"
            
        try:
            self.vault.process_file(path, out_path, seed, 'decrypt')
            messagebox.showinfo("Erfolg", f"Datei entschlüsselt:\n{out_path}")
            if os.name == 'nt': os.startfile(os.path.dirname(out_path))
        except Exception as e:
            messagebox.showerror("Fehler", f"Fehler: {e}")

    # --- Settings Logic ---
    def open_settings(self):
        win = tk.Toplevel(self.root)
        win.title("Konten verwalten")
        win.geometry("500x600")
        
        tk.Label(win, text="Konto hinzufügen", font=("Arial", 12, "bold")).pack(pady=10)
        
        # Auth Type Selection
        auth_type_var = tk.StringVar(value="password")
        fr_auth = tk.Frame(win)
        fr_auth.pack(pady=5)
        tk.Label(fr_auth, text="Authentifizierung:").pack(side=tk.LEFT)
        rb1 = tk.Radiobutton(fr_auth, text="Standard (Passwort/AppPW)", variable=auth_type_var, value="password")
        rb1.pack(side=tk.LEFT)
        rb2 = tk.Radiobutton(fr_auth, text="Gmail OAuth2 (Browser)", variable=auth_type_var, value="oauth2")
        rb2.pack(side=tk.LEFT)

        form = tk.Frame(win)
        form.pack(pady=5)
        
        entries = {}
        fields = [
            ("Name (Intern)", "name"), 
            ("Email Adresse", "email"), 
            ("Passwort (leer bei OAuth)", "password"), 
            ("SMTP Server", "smtp_server"), 
            ("SMTP Port", "smtp_port"),
            ("IMAP Server", "imap_server"), 
            ("IMAP Port", "imap_port")
        ]
        
        for idx, (lbl, key) in enumerate(fields):
            tk.Label(form, text=lbl).grid(row=idx, column=0, sticky="e", padx=5, pady=2)
            e = tk.Entry(form, show="*" if "Passwort" in lbl else None, width=30)
            e.grid(row=idx, column=1, padx=5, pady=2)
            entries[key] = e
            
        def fill_preset(provider):
            entries['smtp_port'].delete(0, tk.END); entries['imap_port'].delete(0, tk.END)
            entries['smtp_server'].delete(0, tk.END); entries['imap_server'].delete(0, tk.END)
            
            if provider == "Gmail":
                auth_type_var.set("oauth2")
                entries['smtp_port'].insert(0, "587")
                entries['imap_port'].insert(0, "993")
                entries['smtp_server'].insert(0, "smtp.gmail.com")
                entries['imap_server'].insert(0, "imap.gmail.com")
                entries['password'].delete(0, tk.END)
                entries['password'].insert(0, "OAUTH-TOKEN")
                
            elif provider == "Outlook":
                auth_type_var.set("password")
                entries['smtp_port'].insert(0, "587")
                entries['imap_port'].insert(0, "993")
                entries['smtp_server'].insert(0, "smtp.office365.com")
                entries['imap_server'].insert(0, "outlook.office365.com")

        btn_fr = tk.Frame(win)
        btn_fr.pack(pady=5)
        tk.Button(btn_fr, text="Gmail (OAuth)", command=lambda: fill_preset("Gmail")).pack(side=tk.LEFT, padx=5)
        tk.Button(btn_fr, text="Outlook (PW)", command=lambda: fill_preset("Outlook")).pack(side=tk.LEFT, padx=5)

        def save():
            data = {k: entries[k].get() for k in entries}
            data['auth_type'] = auth_type_var.get()
            
            if data['auth_type'] == 'oauth2' and not OAUTH_AVAILABLE:
                messagebox.showerror("Fehler", "OAuth Bibliotheken nicht installiert!")
                return
                
            self.acc_manager.add_account(data)
            self.update_account_combo()
            win.destroy()
            messagebox.showinfo("Info", "Konto gespeichert!")

        tk.Button(win, text="Speichern", command=save, bg="#4CAF50", fg="white").pack(pady=20, fill=tk.X, padx=20)

    # --- NETWORK LOGIC (SMTP/IMAP with OAuth Support) ---
    def _authenticate_smtp(self, server, acc):
        if acc['auth_type'] == 'oauth2':
            try:
                print("Hole OAuth Token für SMTP...")
                token = OAuthManager.get_token(acc['email'])
                # SMTP braucht Base64 Encoded String
                auth_str = OAuthManager.build_xoauth2_string(acc['email'], token, base64_encode=True)
                code, resp = server.docmd("AUTH", "XOAUTH2 " + auth_str)
                if code != 235:
                    raise Exception(f"OAuth SMTP fehlgeschlagen: {code} {resp}")
            except Exception as e:
                raise Exception(f"OAuth Fehler: {e}")
        else:
            server.login(acc['email'], acc['password'])

    def _authenticate_imap(self, mail, acc):
        if acc['auth_type'] == 'oauth2':
            try:
                print("Hole OAuth Token für IMAP...")
                token = OAuthManager.get_token(acc['email'])
                # IMAP Lib braucht RAW Bytes (kein Double-Encoding)
                auth_bytes = OAuthManager.build_xoauth2_string(acc['email'], token, base64_encode=False)
                mail.authenticate('XOAUTH2', lambda x: auth_bytes)
            except Exception as e:
                raise Exception(f"OAuth IMAP Fehler: {e}")
        else:
            mail.login(acc['email'], acc['password'])

    # --- Fetch Mail ---
    def fetch_mails(self):
        acc = self.get_current_account()
        if not acc: return

        def task():
            self.status_var.set("Verbinde... (Check Browser bei OAuth!)")
            try:
                mail = imaplib.IMAP4_SSL(acc['imap_server'], int(acc['imap_port']))
                
                self._authenticate_imap(mail, acc)
                
                mail.select("inbox")
                status, messages = mail.search(None, "ALL")
                mail_ids = messages[0].split()[-10:] 
                
                self.fetched_messages = []
                self.tree.delete(*self.tree.get_children())
                
                for i in reversed(mail_ids):
                    try:
                        res, msg_data = mail.fetch(i, "(RFC822)")
                        for response_part in msg_data:
                            if isinstance(response_part, tuple):
                                msg = email.message_from_bytes(response_part[1])
                                subject = "Kein Betreff"
                                if msg["Subject"]:
                                    decoded = email.header.decode_header(msg["Subject"])[0]
                                    subject = decoded[0]
                                    if isinstance(subject, bytes): 
                                        try: subject = subject.decode(decoded[1] if decoded[1] else 'utf-8')
                                        except: subject = str(subject)
                                
                                frm = msg.get("From")
                                date = msg.get("Date")
                                self.fetched_messages.append(msg)
                                self.tree.insert("", "end", values=(frm, subject, date), tags=(len(self.fetched_messages)-1,))
                    except Exception as e:
                        print(f"Fehler bei Mail {i}: {e}")

                mail.close()
                mail.logout()
                self.status_var.set("Abruf fertig.")
            except Exception as e:
                self.status_var.set(f"Fehler: {e}")
                print(e)

        threading.Thread(target=task).start()

    # --- Send Mail ---
    def open_compose(self):
        acc = self.get_current_account()
        if not acc:
            messagebox.showerror("Fehler", "Bitte Konto wählen.")
            return

        win = tk.Toplevel(self.root)
        win.title("Neue Nachricht")
        win.geometry("600x650")
        
        tk.Label(win, text=f"Von: {acc['email']} ({acc.get('auth_type', 'pw')})").pack(anchor="w", padx=10)
        
        fr_head = tk.Frame(win)
        fr_head.pack(fill=tk.X, padx=10)
        tk.Label(fr_head, text="An:").grid(row=0, column=0); to_entry = tk.Entry(fr_head, width=50); to_entry.grid(row=0, column=1)
        tk.Label(fr_head, text="Betreff:").grid(row=1, column=0); subj_entry = tk.Entry(fr_head, width=50); subj_entry.grid(row=1, column=1)
        
        text_area = tk.Text(win, height=15); text_area.pack(fill=tk.BOTH, expand=True, padx=10)
        
        fr_sec = tk.LabelFrame(win, text="Mycelia Security"); fr_sec.pack(fill=tk.X, padx=10, pady=5)
        seed_entry = tk.Entry(fr_sec, width=15); seed_entry.insert(0, str(random.randint(100000,999999))); seed_entry.pack(side=tk.LEFT)
        encrypt_var = tk.BooleanVar(); tk.Checkbutton(fr_sec, text="Verschlüsseln", variable=encrypt_var).pack(side=tk.LEFT)
        
        self.attachments = []
        lbl_att = tk.Label(win, text="0 Anhänge"); lbl_att.pack(anchor="w", padx=10)
        
        def add_att():
            fs = filedialog.askopenfilenames()
            for f in fs: self.attachments.append(f)
            lbl_att.config(text=f"{len(self.attachments)} Anhänge")
        tk.Button(win, text="Anhang +", command=add_att).pack(anchor="w", padx=10)

        def send():
            try:
                msg = MIMEMultipart()
                msg['From'] = formataddr((acc['name'], acc['email']))
                msg['To'] = to_entry.get()
                msg['Subject'] = subj_entry.get()
                
                body = text_area.get("1.0", tk.END)
                use_enc = encrypt_var.get()
                seed = int(seed_entry.get()) if seed_entry.get().isdigit() else 0
                
                if use_enc:
                    tmp = "body.txt"; tmp_enc = "body.myc"
                    with open(tmp, "w", encoding="utf-8") as f: f.write(body)
                    self.vault.process_file(tmp, tmp_enc, seed, 'encrypt')
                    with open(tmp_enc, "rb") as f:
                        part = MIMEBase('application', 'octet-stream')
                        part.set_payload(f.read())
                    encoders.encode_base64(part)
                    part.add_header('Content-Disposition', f"attachment; filename= secure_msg.myc")
                    msg.attach(part)
                    msg.attach(MIMEText("Inhalt mit Mycelia verschlüsselt.", 'plain'))
                    try: os.remove(tmp); os.remove(tmp_enc)
                    except: pass
                else:
                    msg.attach(MIMEText(body, 'plain'))
                
                for path in self.attachments:
                    fn = os.path.basename(path)
                    pp = path
                    if use_enc:
                        ep = path + ".myc"
                        self.vault.process_file(path, ep, seed, 'encrypt')
                        pp = ep; fn += ".myc"
                    
                    with open(pp, "rb") as f:
                        part = MIMEBase("application", "octet-stream"); part.set_payload(f.read())
                    encoders.encode_base64(part)
                    part.add_header("Content-Disposition", f"attachment; filename= {fn}")
                    msg.attach(part)
                    if use_enc: 
                        try: os.remove(ep)
                        except: pass

                # SMTP SENDEN
                import ssl
                context = ssl.create_default_context()
                
                server = smtplib.SMTP(acc['smtp_server'], int(acc['smtp_port']))
                server.starttls(context=context)
                server.ehlo()
                
                self._authenticate_smtp(server, acc)
                
                server.sendmail(acc['email'], to_entry.get(), msg.as_string())
                server.quit()
                
                messagebox.showinfo("Erfolg", "Email gesendet!")
                win.destroy()
            except Exception as e:
                messagebox.showerror("Fehler", str(e))

        tk.Button(win, text="SENDEN", command=send, bg="blue", fg="white").pack(fill=tk.X, padx=10, pady=10)

    def on_mail_double_click(self, event):
        try:
            item = self.tree.selection()[0]
            idx = int(self.tree.item(item, "tags")[0])
            msg = self.fetched_messages[idx]
            self.show_mail_content(msg)
        except: pass

    def show_mail_content(self, msg):
        win = tk.Toplevel(self.root)
        win.title("Nachricht")
        win.geometry("600x500")
        txt = tk.Text(win); txt.pack(fill=tk.BOTH, expand=True)
        
        atts = []
        if msg.is_multipart():
            for part in msg.walk():
                if part.get_content_maintype() == 'multipart': continue
                if part.get("Content-Disposition") is None: continue
                fn = part.get_filename()
                if fn: atts.append((fn, part.get_payload(decode=True)))
                else: 
                     try: txt.insert(tk.END, part.get_payload(decode=True).decode())
                     except: pass
        else:
            txt.insert(tk.END, msg.get_payload(decode=True).decode())
            
        if atts:
            fr = tk.Frame(win); fr.pack(fill=tk.X)
            for fn, data in atts:
                def save(n=fn, d=data):
                    p = filedialog.asksaveasfilename(initialfile=n)
                    if p:
                        with open(p, "wb") as f: f.write(d)
                        if n.endswith(".myc"):
                            s = simpledialog.askstring("Decrypt", "Seed eingeben:")
                            if s: self.vault.process_file(p, p.replace(".myc",""), int(s), 'decrypt')
                tk.Button(fr, text=fn, command=save).pack(side=tk.LEFT)

if __name__ == "__main__":
    root = tk.Tk()
    app = SecureMailClient(root)
    root.mainloop()