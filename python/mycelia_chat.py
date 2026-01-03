import tkinter as tk
from tkinter import scrolledtext, simpledialog, messagebox, filedialog
import socket
import threading
import sys
import struct
import os
import time
import zlib  # WICHTIG: Für Kompression

# Import Engine
try:
    from mycelia_chat_engine import MyceliaChatEngine
except ImportError:
    messagebox.showerror("Fehler", "Engine nicht gefunden (mycelia_chat_engine.py fehlt).")
    sys.exit(1)

# Konfiguration
SERVER_IP = '127.0.0.1' 
PORT = 5555

# Protokoll Typen
TYPE_TEXT = 1
TYPE_FILE = 2

class EmojiPicker(tk.Toplevel):
    def __init__(self, parent, callback):
        super().__init__(parent)
        self.callback = callback
        self.title("Emoji Picker")
        self.geometry("350x200")
        self.configure(bg="#222")
        self.resizable(False, False)
        
        emojis = [
            "😀", "😂", "😎", "🤔", "👍", "👎", "❤️", "💀", 
            "🚀", "🔒", "🍄", "💻", "🔑", "⚠️", "👻", "alien",
            "🤖", "💩", "🔥", "🎉", "✨", "💯", "🙈", "🙉"
        ]
        
        row = 0
        col = 0
        for em in emojis:
            btn = tk.Button(self, text=em, font=("Segoe UI Emoji", 14), 
                            bg="#333", fg="white", borderwidth=0,
                            activebackground="#555", 
                            command=lambda x=em: self.insert_emoji(x))
            btn.grid(row=row, column=col, padx=4, pady=4, sticky="nsew")
            col += 1
            if col > 7:
                col = 0
                row += 1
                
    def insert_emoji(self, char):
        self.callback(char)
        # Optional: Fenster schließen nach Auswahl
        # self.destroy()

class ChatClient:
    def __init__(self, root):
        self.root = root
        self.root.title("Mycelia Encrypted Chat [Zlib Compressed]")
        self.root.geometry("650x550")
        self.root.configure(bg="#121212")
        
        try:
            self.engine = MyceliaChatEngine(0)
        except Exception as e:
            messagebox.showerror("GPU Error", f"Engine Init Fehler:\n{e}")
            sys.exit(1)

        self._build_ui()
        
        # Sicherer Start: Erst PW, dann Connect
        pwd = simpledialog.askstring("Sicherheit", "Shared Secret (Passwort):", show='*', parent=root)
        if pwd:
            self.engine.set_password(pwd)
            self.log_ui(f"[System] Kanal gesichert (Hash aktiv).", "sys")
        else:
            self.log_ui("[WARNUNG] Kein Passwort! Verbindung unsicher.", "error")
        
        self.client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.client.connect((SERVER_IP, PORT))
            threading.Thread(target=self.receive_msg, daemon=True).start()
            self.log_ui(f"[System] Verbunden mit {SERVER_IP}:{PORT}", "sys")
        except:
            self.log_ui(f"[System] Server offline oder nicht erreichbar.", "error")

    def _build_ui(self):
        self.txt_area = scrolledtext.ScrolledText(self.root, bg="#1e1e1e", fg="#e0e0e0", font=("Segoe UI Emoji", 10))
        self.txt_area.pack(padx=10, pady=10, fill="both", expand=True)
        self.txt_area.tag_config("sys", foreground="#888", font=("Consolas", 9))
        self.txt_area.tag_config("me", foreground="#00ff99", font=("Segoe UI", 10, "bold"))
        self.txt_area.tag_config("peer", foreground="#ff9900", font=("Segoe UI", 10, "bold"))
        self.txt_area.tag_config("file", foreground="#00ccff", underline=1)
        self.txt_area.tag_config("error", foreground="#ff5555")
        self.txt_area.config(state='disabled')

        frm = tk.Frame(self.root, bg="#121212")
        frm.pack(padx=10, pady=10, fill="x")
        
        tk.Button(frm, text="📎", bg="#333", fg="white", font=("Arial", 12), command=self.send_file_dialog).pack(side="left", padx=2)
        tk.Button(frm, text="😊", bg="#333", fg="white", font=("Arial", 12), command=self.open_emojis).pack(side="left", padx=2)
        
        self.entry_msg = tk.Entry(frm, bg="#333", fg="white", font=("Segoe UI Emoji", 11), relief="flat", insertbackground="white")
        self.entry_msg.pack(side="left", fill="x", expand=True, ipady=4, padx=5)
        self.entry_msg.bind("<Return>", self.send_text)
        
        tk.Button(frm, text="➤", bg="#00ff99", fg="black", font=("Arial", 10, "bold"), command=self.send_text).pack(side="right")

    def log_ui(self, text, tag=None):
        self.txt_area.config(state='normal')
        self.txt_area.insert('end', text + '\n', tag)
        self.txt_area.see('end')
        self.txt_area.config(state='disabled')

    def open_emojis(self):
        EmojiPicker(self.root, lambda c: self.entry_msg.insert(tk.INSERT, c))

    # --- NETZWERK KERN ---

    def _send_packet(self, raw_payload):
        """Verschlüsselt und sendet mit 4-Byte Längen-Header."""
        try:
            # 1. Verschlüsseln (GPU)
            encrypted = self.engine.encrypt_bytes(raw_payload)
            # 2. Framing (Länge + Daten)
            # I = unsigned int (4 bytes)
            packet = struct.pack("I", len(encrypted)) + encrypted
            self.client.sendall(packet)
        except Exception as e:
            self.log_ui(f"[Error] Sende-Fehler: {e}", "error")

    def _recv_exactly(self, n):
        """Liest exakt n Bytes, wartet bei Fragmentierung."""
        data = b''
        while len(data) < n:
            try:
                chunk = self.client.recv(n - len(data))
                if not chunk: return None
                data += chunk
            except: return None
        return data

    # --- SENDEN ---

    def send_text(self, event=None):
        msg = self.entry_msg.get()
        if not msg: return
        self.log_ui(f"Me: {msg}", "me")
        self.entry_msg.delete(0, 'end')
        # Text senden (Typ 1)
        payload = struct.pack("B", TYPE_TEXT) + msg.encode('utf-8')
        self._send_packet(payload)

    def send_file_dialog(self):
        path = filedialog.askopenfilename()
        if not path: return
        
        # Check size (Max 50MB für Demo-Stabilität)
        if os.path.getsize(path) > 50 * 1024 * 1024:
            messagebox.showwarning("Limit", "Datei zu groß (Max 50MB).")
            return

        filename = os.path.basename(path)
        
        try:
            # 1. Datei lesen
            with open(path, 'rb') as f:
                raw_data = f.read()
            
            orig_size = len(raw_data)
            
            # 2. ZIPPEN! 
            # Level 6 ist guter Kompromiss Speed/Größe
            zipped_data = zlib.compress(raw_data, level=6)
            zip_size = len(zipped_data)
            ratio = (1 - (zip_size/orig_size)) * 100
            
            self.log_ui(f"Me: Sende '{filename}' ({orig_size/1024:.1f}KB -> {zip_size/1024:.1f}KB | -{ratio:.1f}%)...", "file")
            
            # 3. Payload bauen: Typ(1) + NameLen(2) + Name + ZippedData
            fn_bytes = filename.encode('utf-8')
            # B = Typ, H = Namenslänge (unsigned short)
            payload = struct.pack("B", TYPE_FILE) + struct.pack("H", len(fn_bytes)) + fn_bytes + zipped_data
            
            self._send_packet(payload)
            
        except Exception as e:
            self.log_ui(f"[Error] Dateifehler: {e}", "error")

    # --- EMPFANGEN ---

    def receive_msg(self):
        while True:
            try:
                # 1. Header (Länge) lesen
                len_bytes = self._recv_exactly(4)
                if not len_bytes: break
                packet_len = struct.unpack("I", len_bytes)[0]
                
                # 2. Datenpaket lesen
                packet = self._recv_exactly(packet_len)
                if not packet: break
                
                # 3. Entschlüsseln (GPU)
                decrypted = self.engine.decrypt_packet_to_bytes(packet)
                if not decrypted:
                    self.log_ui("[System] Krypto-Fehler / Falsches Passwort", "error")
                    continue
                
                # 4. Protokoll Parsen
                msg_type = decrypted[0]
                content = decrypted[1:]
                
                if msg_type == TYPE_TEXT:
                    try:
                        text = content.decode('utf-8')
                        self.log_ui(f"Peer: {text}", "peer")
                    except:
                        self.log_ui("[System] Encoding Fehler (UTF-8)", "error")
                        
                elif msg_type == TYPE_FILE:
                    try:
                        # Name extrahieren
                        fn_len = struct.unpack("H", content[:2])[0]
                        filename = content[2 : 2 + fn_len].decode('utf-8')
                        
                        # Der Rest sind die gezippten Daten
                        zipped_data = content[2 + fn_len :]
                        
                        self.log_ui(f"Peer sendet Datei: {filename}...", "peer")
                        
                        # 5. ENTZIPPEN! 
                        try:
                            file_data = zlib.decompress(zipped_data)
                            self.save_incoming_file(filename, file_data)
                        except zlib.error:
                            self.log_ui("[System] FEHLER: Datei beschädigt (Zip Checksum Error)!", "error")
                            
                    except Exception as e:
                        self.log_ui(f"[System] Dateifehler beim Empfang: {e}", "error")
                        
                else:
                    self.log_ui(f"[System] Unbekannter Nachrichtentyp: {msg_type}", "error")

            except ConnectionResetError:
                self.log_ui("[System] Verbindung unterbrochen.", "error")
                break
            except Exception as e:
                print(f"Recv Loop Error: {e}")
                break

    def save_incoming_file(self, filename, data):
        save_dir = "Mycelia_Downloads"
        if not os.path.exists(save_dir): os.makedirs(save_dir)
        
        save_path = os.path.join(save_dir, filename)
        # Unique Name falls vorhanden (timestamp)
        if os.path.exists(save_path):
            base, ext = os.path.splitext(filename)
            save_path = os.path.join(save_dir, f"{base}_{int(time.time())}{ext}")
            
        try:
            with open(save_path, 'wb') as f:
                f.write(data)
            self.log_ui(f"   -> Gespeichert: {save_path} ({len(data)/1024:.1f} KB)", "file")
        except Exception as e:
            self.log_ui(f"   -> Speicherfehler: {e}", "error")

if __name__ == "__main__":
    root = tk.Tk()
    app = ChatClient(root)
    root.mainloop()