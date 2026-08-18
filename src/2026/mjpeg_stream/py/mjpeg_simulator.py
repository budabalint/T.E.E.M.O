import serial
import time
import struct
import os

# --- KONSTANSOK ---
PACKET_SIZE = 126
PAYLOAD_SIZE = 120
SYNC_BYTE = 0xFE
TYPE_MJPEG = 0xDD
TYPE_FEC = 0xFF

# Modulo-9 és szekvencia határok
SEQ_MAX = 251

# --- CRC8-CCITT IMPLEMENTÁCIÓ ---
# Polinom: 0x07, Kezdeti: 0x00, RefIn/Out: False
def calc_crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc = (crc << 1)
            crc &= 0xFF
    return crc

# --- PROTOKOLL ÁLLAPOTGÉP ÉS ADÓ ---
class DDEEFF_Transmitter:
    def __init__(self, port, baudrate=115200):
        # Soros port inicializálása
        print(f"[*] Soros port megnyitása: {port} @ {baudrate} bps...")
        self.ser = serial.Serial(port, baudrate, timeout=1)
        
        # Állapotváltozók
        self.seq = 0
        self.frame_id = -1 # Első képkockánál lesz 0
        self.rrt_counter = -1 # Relatív Reset Távolság (csomagokban)
        
        # FEC puffer
        self.fec_group = [] # Az aktuális 8 adatcsomagot tárolja
        
    def create_and_send_packet(self, payload: bytearray, active_mask_index: int = -1):
        """
        Létrehoz egy 0xDD adatcsomagot, és kezeli a Modulo-9 FEC szabályt.
        """
        # 1. MASK bájt meghatározása (RRT Állapotgép)
        mask = 255 # Alapértelmezett: Nincs infó
        
        if active_mask_index != -1:
            # Aktív marker van a csomagban (0-119)
            mask = active_mask_index
            self.rrt_counter = 0 # Reset történt, számláló indul
        else:
            if self.rrt_counter >= 0:
                self.rrt_counter += 1
                if self.rrt_counter <= 130:
                    mask = 120 + self.rrt_counter # Relatív távolság 121-250
                else:
                    self.rrt_counter = -1 # Túl messze van, visszaáll 255-re
                    
        # FRAME_ID határok kezelése (0-255)
        current_frame_id = max(0, self.frame_id) % 256
        
        # 2. Fejléc összerakása [0..4] + Payload [5..124]
        packet_without_crc = struct.pack(
            '>BBBBB', # 5 db unsigned char (Big-Endian)
            SYNC_BYTE,
            TYPE_MJPEG,
            self.seq,
            current_frame_id,
            mask
        ) + payload

        # 3. CRC8 számítása [125]
        crc8 = calc_crc8(packet_without_crc)
        final_packet = packet_without_crc + bytes([crc8])
        
        # 4. Csomag elküldése és FEC pufferbe rakása
        self.ser.write(final_packet)
        self.fec_group.append(final_packet)
        
        # Logolás (opcionális, kikapcsolható a sebesség miatt)
        # print(f"Adatcsomag elküldve | SEQ: {self.seq} | FRAME_ID: {current_frame_id} | MASK: {mask} | CRC: {hex(crc8)}")
        
        # Szekvencia növelése
        self.seq = (self.seq + 1) % (SEQ_MAX + 1)
        
        # 5. Modulo-9 FEC szabály ellenőrzése
        if len(self.fec_group) == 8:
            self.send_fec_packet()

    def send_fec_packet(self):
        """
        Generál egy 0xFF FEC paritáscsomagot az előző 8 adatcsomag alapján.
        """
        fec_id = 0
        fec_mask = 0
        fec_payload = bytearray(PAYLOAD_SIZE)
        
        # XOR számítások
        for pkt in self.fec_group:
            fec_id ^= pkt[3]   # ID XOR
            fec_mask ^= pkt[4] # MASK XOR
            for i in range(PAYLOAD_SIZE):
                fec_payload[i] ^= pkt[5 + i] # Payload oszloponkénti XOR
                
        # FEC fejléc összerakása
        packet_without_crc = struct.pack(
            '>BBBBB',
            SYNC_BYTE,
            TYPE_FEC,
            self.seq,
            fec_id,
            fec_mask
        ) + fec_payload
        
        # CRC8
        crc8 = calc_crc8(packet_without_crc)
        final_packet = packet_without_crc + bytes([crc8])
        
        # FEC küldése
        self.ser.write(final_packet)
        # print(f"--- FEC Csomag elküldve | SEQ: {self.seq} | FEC_ID: {fec_id} | FEC_MASK: {fec_mask} ---")
        
        # Szekvencia növelése és puffer ürítése
        self.seq = (self.seq + 1) % (SEQ_MAX + 1)
        self.fec_group.clear()

# --- MJPEG FELDOLGOZÓ ÉS STREAMELŐ ---
def stream_mjpeg(file_path: str, port: str):
    tx = DDEEFF_Transmitter(port)
    
    # Fájl beolvasása a memóriába (6MB simán elfér)
    print(f"[*] Fájl beolvasása: {file_path}")
    with open(file_path, "rb") as f:
        mjpeg_data = f.read()
        
    print(f"[*] Fájlméret: {len(mjpeg_data)} bájt. Streamelés indítása...")
    
    idx = 0
    file_len = len(mjpeg_data)
    
    payload_buffer = bytearray()
    active_mask_idx = -1
    
    while idx < file_len:
        # 1. SOI (Start of Image) keresése - FF D8
        if idx + 1 < file_len and mjpeg_data[idx] == 0xFF and mjpeg_data[idx+1] == 0xD8:
            tx.frame_id += 1 # Új képkocka
            active_mask_idx = len(payload_buffer) # Aktuális bájthely a payloadon belül
            # "SOI Strip" szabály: A markert KIHAGYJUK a payloadból!
            idx += 2
            continue
            
        # 2. DHT (Huffman-tábla) keresése - FF C4
        if idx + 1 < file_len and mjpeg_data[idx] == 0xFF and mjpeg_data[idx+1] == 0xC4:
            # "No-DHT" szabály: Kihagyjuk a markert ÉS a tartalmát
            if idx + 3 < file_len:
                # A hossz a marker utáni 2 bájt (Big-Endian uint16)
                dht_len = struct.unpack('>H', mjpeg_data[idx+2:idx+4])[0]
                idx += 2 + dht_len # Átugorjuk a markert (2) + a hosszt és adatokat
                continue

        # Ha nem marker, vagy olyan marker ami maradhat, rögzítjük a bájtot
        payload_buffer.append(mjpeg_data[idx])
        idx += 1
        
        # 3. Payload megtelt -> Csomag küldése
        if len(payload_buffer) == PAYLOAD_SIZE:
            tx.create_and_send_packet(payload_buffer, active_mask_idx)
            # Puffer és mask reset a következő csomaghoz
            payload_buffer = bytearray()
            active_mask_idx = -1
            
            # Késleltetés a soros port szimulációjához (opcionális, hardvertől függ)
            # time.sleep(0.005) 

    # 4. Zero-Padding (Nulla-kitöltés) szabály az utolsó csonka csomaghoz
    if len(payload_buffer) > 0:
        padding_needed = PAYLOAD_SIZE - len(payload_buffer)
        payload_buffer.extend(b'\x00' * padding_needed)
        tx.create_and_send_packet(payload_buffer, active_mask_idx)
        
    print("[*] Streamelés befejeződött!")
    tx.ser.close()

# --- FŐPROGRAM ---
if __name__ == "__main__":
    # Paraméterek: módosítsd a saját rendszerednek megfelelően!
    # Windowson pl. 'COM3', Linuxon pl. '/dev/ttyUSB0'
    # Ha hardver nélkül akarod tesztelni a kód futását, használhatod a 'loop://' (pyserial loopback) portot.
    
    SERIAL_PORT = 'COM3'  # Cseréld ki pl. 'COM3'-ra
    MJPEG_FILE = 'out16M.mjpeg' # Cseréld ki a 6MB-os fájlod nevére
    
    # Létrehozunk egy dummy fájlt a teszthez, ha nem létezik
    if not os.path.exists(MJPEG_FILE):
        print(f"[!] Nem található a {MJPEG_FILE}, létrehozok egy tesztfájlt (FF D8 és FF C4 markerekkel)...")
        with open(MJPEG_FILE, 'wb') as f:
            # Fake MJPEG: SOI + Dummy data + DHT + Dummy data
            f.write(b'\xFF\xD8' + b'\xAA'*300)
            f.write(b'\xFF\xC4\x00\x10' + b'\xBB'*14) # Dummy DHT
            f.write(b'\xCC'*500)
            f.write(b'\xFF\xD9') # EOI
            
    try:
        stream_mjpeg(MJPEG_FILE, SERIAL_PORT)
    except Exception as e:
        print(f"Hiba történt: {e}")