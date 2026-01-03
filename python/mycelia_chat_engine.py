import ctypes
import os
import struct
import sys
import threading
import numpy as np

# --- Pfad-Logik (PyInstaller kompatibel) ---
def resource_path(relative_path):
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)

DLL_NAME = "CC_OpenCl.dll" if os.name == "nt" else "CC_OpenCl.so"
LIB_PATH = resource_path(DLL_NAME)

# Fallback Suche
if not os.path.exists(LIB_PATH):
    possible_paths = [
        os.path.join("build", DLL_NAME),
        os.path.join("../bin", DLL_NAME),
        os.path.join("bin", DLL_NAME),
        DLL_NAME
    ]
    for p in possible_paths:
        if os.path.exists(p):
            LIB_PATH = os.path.abspath(p)
            break

try:
    if os.name == "nt":
        cl = ctypes.CDLL(LIB_PATH, winmode=0)
    else:
        cl = ctypes.CDLL(LIB_PATH)
except OSError as exc:
    print(f"CRITICAL DLL ERROR: {exc}")
    # Wir lassen es laufen, damit die GUI den Fehler anzeigen kann, 
    # aber in Produktion sollte hier Schluss sein.

# --- C-Definitionen ---
class HPIOAgent(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float), ("energy", ctypes.c_float), ("coupling", ctypes.c_float)]

cl.initialize_gpu.argtypes = [ctypes.c_int]
cl.subqg_set_deterministic_mode.argtypes = [ctypes.c_int, ctypes.c_ulonglong]
cl.subqg_initialize_state.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
cl.subqg_simulation_step.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
cl.subqg_debug_read_channel.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int]

# Konstanten
GRID_SIZE = 256 * 256  # 65536 Floats = Ein Key-Block
C_LOCK = threading.Lock()

class MyceliaChatEngine:
    def __init__(self, gpu_index: int = 0):
        self.gpu = gpu_index
        try:
            res = cl.initialize_gpu(gpu_index)
            print(f"[Engine] GPU Init Result: {res}")
        except Exception as e:
            print(f"[Engine] Init Failed: {e}")
            
        self.password_hash = 0

    def set_password(self, password: str):
        import hashlib
        if not password:
            self.password_hash = 0
            return
        h = hashlib.sha256(password.encode('utf-8')).digest()
        # Nimm die ersten 8 Bytes als uint64 für die XOR Maske
        self.password_hash = struct.unpack("Q", h[:8])[0]

    def _generate_key_block(self, seed: int, block_index: int) -> np.ndarray:
        """
        Erzeugt einen 64KB Schlüsselblock für einen spezifischen Index.
        Dies ist die 'Bio-CTR' Logik aus dem SDK V4.
        """
        with C_LOCK:
            # 1. Deterministischer Seed für DIESEN Block
            # (MasterSeed + Offset * Prime) verhindert Wiederholungen
            current_seed = seed + (block_index * 7919)
            
            # 2. GPU Reset & Simulation
            cl.subqg_set_deterministic_mode(1, ctypes.c_ulonglong(current_seed))
            cl.subqg_initialize_state(self.gpu, 0.5, 0.5, 0.005, 0.5)
            cl.subqg_simulation_step(self.gpu, 0.5, 0.5, 0.5, None, None, None, None, None, None, None, 0)
            
            # 3. Auslesen
            raw_buffer = np.zeros(GRID_SIZE, dtype=np.float32)
            cl.subqg_debug_read_channel(
                self.gpu,
                0,
                raw_buffer.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                GRID_SIZE,
            )

            # 4. Hashing (Float -> Byte)
            # Nutzt die rohen Bits der Floats (oder Ints im Enterprise Kernel)
            key_int = raw_buffer.view(np.uint32)
            key_int = (key_int ^ (key_int >> 16)) * 0x45D9F3B
            key_bytes = (key_int & 0xFF).astype(np.uint8)
            
            return key_bytes

    def encrypt_bytes(self, data: bytes) -> bytes:
        """
        Verschlüsselt Daten beliebiger Größe (Text oder Zipped Files).
        Nutzt Block-Streaming im RAM.
        """
        # 1. Master Seed würfeln
        msg_seed_bytes = os.urandom(8)
        master_seed = struct.unpack("Q", msg_seed_bytes)[0]

        # 2. Daten in Blöcke zerteilen und verschlüsseln
        encrypted_parts = []
        data_view = memoryview(data)
        total_len = len(data)
        
        # Grid-Größe in Bytes (da 1 Byte pro Zelle im Key generiert wird)
        BLOCK_SIZE = GRID_SIZE 
        
        for i in range(0, total_len, BLOCK_SIZE):
            block_index = i // BLOCK_SIZE
            
            # Key für diesen Abschnitt generieren
            key_chunk = self._generate_key_block(master_seed, block_index)
            
            # Daten-Slice holen
            end = min(i + BLOCK_SIZE, total_len)
            data_chunk = np.frombuffer(data_view[i:end], dtype=np.uint8)
            
            # Key zuschneiden, falls letzter Block kleiner ist
            current_key = key_chunk[:len(data_chunk)]
            
            # XOR
            enc_chunk = np.bitwise_xor(data_chunk, current_key)
            encrypted_parts.append(enc_chunk.tobytes())

        full_encrypted_data = b''.join(encrypted_parts)

        # 3. Paket schnüren
        # Seed maskieren mit Passwort
        masked_seed = master_seed ^ self.password_hash
        
        # Header: [MaskedSeed 8B] + [DataLen 4B] + [EncryptedData...]
        # 'Q' = unsigned long long (8), 'I' = unsigned int (4)
        packet = struct.pack("Q", masked_seed) + struct.pack("I", len(full_encrypted_data)) + full_encrypted_data
        
        return packet

    def decrypt_packet_to_bytes(self, packet: bytes) -> bytes:
        """
        Entschlüsselt ein Paket und gibt ROHE BYTES zurück.
        """
        try:
            if len(packet) < 12: return None
            
            # 1. Header lesen
            masked_seed = struct.unpack("Q", packet[:8])[0]
            msg_len = struct.unpack("I", packet[8:12])[0]
            
            if len(packet) < 12 + msg_len:
                # Paket unvollständig (sollte durch Framing im Chat-Client abgefangen werden, aber zur Sicherheit)
                return None 

            encrypted_data = packet[12 : 12 + msg_len]
            
            # Seed demaskieren
            master_seed = masked_seed ^ self.password_hash
            
            # 2. Entschlüsselung (Blockweise)
            decrypted_parts = []
            data_view = memoryview(encrypted_data)
            total_len = len(encrypted_data)
            BLOCK_SIZE = GRID_SIZE
            
            for i in range(0, total_len, BLOCK_SIZE):
                block_index = i // BLOCK_SIZE
                
                # Key rekonstruieren
                key_chunk = self._generate_key_block(master_seed, block_index)
                
                end = min(i + BLOCK_SIZE, total_len)
                enc_chunk = np.frombuffer(data_view[i:end], dtype=np.uint8)
                current_key = key_chunk[:len(enc_chunk)]
                
                # XOR (Reversibel)
                dec_chunk = np.bitwise_xor(enc_chunk, current_key)
                decrypted_parts.append(dec_chunk.tobytes())

            return b''.join(decrypted_parts)
            
        except Exception as e:
            print(f"Decrypt Error: {e}")
            return None