#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =============================================================================
# MJPEG Simulator V3 - "God-Tier" Architecture
# C++ TRANSLATION COMMENTS INCLUDED
# =============================================================================
# FŐBB JAVÍTÁSOK:
#   - Ctrl+C azonnal leállít (threading.Event + signal handler)
#   - Csomagvesztés szimuláció --loss paraméterrel (0-100%)
#   - Szegmentált interleaving 20-as csoportokban (progresszív betöltés)
#   - Fec kódolás: K=10/M=10 fejlécnek, K=20/M=12 adatnak
# =============================================================================

import sys
import os
import time
import struct
import math
import argparse
import threading
import signal
import random

# Add shared to path
# C++: #include "../shared/fec_codec.h"
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'shared')))
# pyrefly: ignore [missing-import]
from fec_codec import FecCodec

# --- Protocol Constants ---
# C++: constexpr uint8_t SYNC_BYTE = 0xFE;
# C++: constexpr int PACKET_SIZE = 127;
# C++: constexpr int PAYLOAD_SIZE = 122;
PACKET_SIZE  = 127
PAYLOAD_SIZE = 121
SYNC_BYTE    = 0xFE
TYPE_MJPEG   = 0xDD
TYPE_FEC     = 0xFF

# UEP (Unequal Error Protection) Konfiguráció
# Fejléc: K=10 adatcsomag + M=10 paritás (100% redundancia → nagyon fontos!)
# Adat  : K=20 adatcsomag + M=12 paritás  (60% redundancia → jó kompromisszum)
HEADER_K = 10
HEADER_M = 10
DATA_K   = 20
DATA_M   = 12

# Interleaving csoport mérete (chunk-ok száma / csoport)
# Ez határozza meg, milyen "korai" megjelenik az első részleges kép
INTERLEAVE_GROUP = 20

# Globális leállítás esemény (Ctrl+C kezelés)
# C++: std::atomic<bool> g_stop_flag(false);
g_stop = threading.Event()


# =============================================================================
# CRC8 - Hibás csomag kiszűrése
# C++: uint8_t calc_crc8(const uint8_t* data, int len) { ... }
# =============================================================================
def calc_crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


# =============================================================================
# UepConfig - Konfigurációs struktúra
# C++: struct UepConfig { int K, M, N; };
# =============================================================================
class UepConfig:
    def __init__(self, k: int, m: int):
        self.K = k   # Adatcsomagok száma
        self.M = m   # Paritáscsomagok száma
        self.N = k + m  # Összes csomag egy blokkban


# =============================================================================
# UepChunker - JPEG képkocka felosztása FEC blokkokra
# C++: class UepChunker { ... };
# =============================================================================
class UepChunker:
    """
    A JPEG képkocka bájtjait feldarabolja:
      - Chunk 0 (fejléc): az első HEADER_K*PAYLOAD_SIZE bájt → erős FEC védelemmel
      - Chunk 1..N (adat): a maradék DATA_K*PAYLOAD_SIZE bájtonként
    """

    def __init__(self, payload_size: int = PAYLOAD_SIZE):
        self.payload_size = payload_size
        self.header_cfg   = UepConfig(HEADER_K, HEADER_M)
        self.data_cfg     = UepConfig(DATA_K, DATA_M)
        # C++: FecCodec header_codec(HEADER_K, HEADER_M);
        # C++: FecCodec data_codec(DATA_K, DATA_M);
        self.header_codec = FecCodec(HEADER_K, HEADER_M)
        self.data_codec   = FecCodec(DATA_K, DATA_M)

    def chunk_frame(self, frame_bytes: bytes) -> list:
        """
        C++: std::vector<Chunk> chunk_frame(const std::vector<uint8_t>& frame);
        Visszatér: lista of dict { id, data_packets, cfg, codec }
        """
        chunks = []

        # --- Fejléc blokk (Chunk 0) ---
        # Fix méret: HEADER_K * PAYLOAD_SIZE bájt (többi zero-padded)
        hdr_size = self.header_cfg.K * self.payload_size
        hdr_bytes = frame_bytes[:hdr_size]                   # Pontosan hdr_size bájt (esetleg kevesebb)
        dat_bytes = frame_bytes[hdr_size:]                   # Maradék → adat blokkok

        hdr_packets = self._pack_bytes(hdr_bytes, self.header_cfg.K)
        chunks.append({
            "id":     0,
            "type":   "HEADER",
            "data":   hdr_packets,
            "cfg":    self.header_cfg,
            "codec":  self.header_codec
        })

        # --- Adat blokkok (Chunk 1..N) ---
        blk_size = self.data_cfg.K * self.payload_size
        num_data_chunks = math.ceil(len(dat_bytes) / blk_size) if dat_bytes else 0

        for i in range(num_data_chunks):
            start = i * blk_size
            end   = start + blk_size
            slice_b = dat_bytes[start:end]
            data_packets = self._pack_bytes(slice_b, self.data_cfg.K)
            chunks.append({
                "id":     i + 1,
                "type":   "DATA",
                "data":   data_packets,
                "cfg":    self.data_cfg,
                "codec":  self.data_codec
            })

        return chunks

    def _pack_bytes(self, data: bytes, num_packets: int) -> list:
        """
        C++: std::vector<std::vector<uint8_t>> pack_bytes(const std::vector<uint8_t>& data, int K);
        Feldarabolja a bájtokat num_packets darab PAYLOAD_SIZE méretű csomagra (zero-padding).
        """
        packets = []
        for i in range(num_packets):
            start  = i * self.payload_size
            end    = start + self.payload_size
            pkt    = bytearray(data[start:end])
            # Zero-padding, ha szükséges (utolsó csomagnál)
            if len(pkt) < self.payload_size:
                pkt.extend(b'\x00' * (self.payload_size - len(pkt)))
            packets.append(pkt)
        return packets


# =============================================================================
# PacketEncoder - Egy nyers csomag elkészítése az RF kerethez
# C++: class PacketEncoder { ... };
# =============================================================================
class PacketEncoder:
    """
    Packet formátum (127 bájt):
      [0]     : SYNC (0xFE)
      [1]     : frame_id (0-255, körkörösen)
      [2]     : chunk_id (0-255)
      [3]     : pkt_idx  (0-255, adaton belüli sorszám)
      [4-125] : payload  (122 bájt)
      [126]   : CRC8
    C++: struct Packet { uint8_t sync, frame_id, chunk_id, pkt_idx, payload[122], crc; };
    """

    def create_packet(self, frame_id: int, chunk_id: int, pkt_idx: int, packet_type: int, payload: bytearray) -> bytes:
        # C++: PacketEncoder::create_packet(...)
        hdr = struct.pack('>BBBBB', SYNC_BYTE, packet_type, frame_id & 0xFF, chunk_id & 0xFF, pkt_idx & 0xFF)
        body = hdr + bytes(payload)
        crc  = calc_crc8(body)
        return body + bytes([crc])


# =============================================================================
# Interleaver - Szegmentált összefésülés burst hibák ellen
# C++: class Interleaver { ... };
# =============================================================================
class Interleaver:
    """
    A chunkokat INTERLEAVE_GROUP méretű csoportokra bontja, majd minden csoporton belül
    az összefésülést PACKET-szinten végzi: 0. csomag az összes chunkból, majd 1. csomag, stb.

    Miért csoportokban és nem a teljes frame-en?
      - A teljes frame-szintű interleaving esetén a dekóder az UTOLSÓ csomag megérkezéséig
        semmit sem tud megjeleníteni.
      - A csoportos interleaving esetén az első csoport megérkezése után a kép TETEJE
        megjelenik, majd fokozatosan töltődik ki a kép.
      - Ugyanakkor a csoporton BELÜL teljes burst-védelmet nyújtunk.
    """

    def __init__(self, group_size: int = INTERLEAVE_GROUP):
        # C++: Interleaver(int group_size) : group_size_(group_size) {}
        self.group_size = group_size

    def build_stream(self, frame_id: int, chunks: list, encoder: PacketEncoder) -> list:
        """
        C++: std::vector<Packet> build_stream(int frame_id, const std::vector<Chunk>& chunks, PacketEncoder& enc);
        Visszatér: a küldendő csomagok listája (raw bytes).
        """
        stream = []

        # Iterálunk a chunk-csoportokon
        for grp_start in range(0, len(chunks), self.group_size):
            grp = chunks[grp_start: grp_start + self.group_size]

            # --- FEC kódolás a csoporton belüli összes chunkra ---
            # C++: for (auto& chunk : grp) chunk.encoded = codec.encode(chunk.data);
            encoded_grp = []
            max_pkts = 0
            for chunk in grp:
                data_pkts   = chunk["data"]
                parity_pkts = chunk["codec"].encode(data_pkts)
                all_pkts    = data_pkts + parity_pkts          # K + M csomag
                encoded_grp.append({"id": chunk["id"], "pkts": all_pkts, "K": chunk["cfg"].K})
                max_pkts = max(max_pkts, len(all_pkts))

            # --- Összefésülés: pkt_idx=0 az összes chunktól, majd pkt_idx=1, stb. ---
            # C++: for (int pi = 0; pi < max_pkts; pi++)
            #        for (auto& ec : encoded_grp)
            #          if (pi < ec.pkts.size()) stream.push_back(encoder.create_packet(...));
            for pkt_idx in range(max_pkts):
                for ec in encoded_grp:
                    if pkt_idx < len(ec["pkts"]):
                        is_fec = (pkt_idx >= ec["K"])
                        packet_type = TYPE_FEC if is_fec else TYPE_MJPEG
                        raw = encoder.create_packet(frame_id, ec["id"], pkt_idx, packet_type, ec["pkts"][pkt_idx])
                        stream.append(raw)

        return stream


# =============================================================================
# SerialTransmitter - Soros port írás + csomagvesztés szimuláció
# C++: class SerialTransmitter { ... };
# =============================================================================
class SerialTransmitter:
    """
    Szimulált adóegység:
      - Írja az adatokat a soros portba (valódi hardveren)
      - loss_rate > 0 esetén véletlenszerűen elveszít csomagokat (teszteléshez)
      - Globális időzítővel tartja a pontos baudrate-et, elkerülve a Windows sleep overhead-et.
      - A g_stop.is_set() esetén azonnal leáll (Ctrl+C támogatás!)
    """

    def __init__(self, port: str, baudrate: int, loss_rate: float = 0.0):
        # C++: SerialTransmitter(const std::string& port, int baudrate, float loss_rate)
        import serial
        self.ser        = serial.Serial(port, baudrate, timeout=1)
        self.baudrate   = baudrate
        # 8N1 kódolás: 10 bit / bájt (1 start + 8 adat + 1 stop)
        self.byte_time  = 10.0 / baudrate   # másodperc / bájt
        self.loss_rate  = loss_rate         # 0.0 - 100.0 %
        
        # Globális szinkronizációhoz
        self.target_time = time.monotonic()

    def send_packet(self, data: bytes) -> bool:
        """
        C++: bool send_packet(const Packet& pkt);
        Visszatér: True ha tényleg elküldtük, False ha elveszítettük.
        Figyel a g_stop eventre, így Ctrl+C azonnal megszakítja!
        """
        if g_stop.is_set():
            return False

        # Csomagvesztés szimuláció
        lost = (self.loss_rate > 0 and random.uniform(0.0, 100.0) < self.loss_rate)

        if not lost:
            try:
                self.ser.write(data)
            except Exception as e:
                print(f"[!] Serial write hiba: {e}")
                return False

        # Fizikai átviteli idő szimulálása GLOBÁLIS időzítővel
        self.target_time += (len(data) * self.byte_time)
        
        now = time.monotonic()
        if self.target_time > now:
            # Csak akkor alszunk, ha megelőztük az időt.
            # Ezzel elkerüljük a Windows 15ms-os sleep túlkompenzálását.
            sleep_time = self.target_time - now
            if sleep_time > 0.01:
                # 10ms-nál nagyobb csúszásnál engedjük a rendszert aludni
                time.sleep(sleep_time)

        return not lost

    def close(self):
        # C++: ~SerialTransmitter() { ser_.close(); }
        if self.ser.is_open:
            self.ser.close()


# =============================================================================
# MJPEG Frame Splitter - MJPEG fájl feldarabolása JPEG képkockákra
# C++: std::vector<std::vector<uint8_t>> split_mjpeg(const std::vector<uint8_t>& data);
# =============================================================================
def inject_rst_table(frame_data: bytes) -> bytes:
    """
    Per-chunk RST marker darabszám beágyazása APP3 markerbe.
    Formátum: 'RST\0' + total_rst(2B) + chunk_0_count(1B) + chunk_1_count(1B) + ...
    A dekóder így pontosan tudja, hány dummy RST markert kell injektálnia
    egy elveszett chunk helyére, ANÉLKÜL hogy bájt-offseteket kellene számolnia.
    """
    sos_idx = frame_data.find(b'\xFF\xDA')
    if sos_idx == -1:
        return frame_data

    # RST markerek keresése (csak a scan data-ban, SOS után)
    sos_len = (frame_data[sos_idx + 2] << 8) + frame_data[sos_idx + 3]
    scan_start = sos_idx + 2 + sos_len
    rst_positions = []
    idx = scan_start
    while idx < len(frame_data) - 1:
        idx = frame_data.find(b'\xFF', idx)
        if idx == -1 or idx >= len(frame_data) - 1:
            break
        marker = frame_data[idx + 1]
        if 0xD0 <= marker <= 0xD7:
            rst_positions.append(idx)
        elif marker == 0xD9:  # EOI
            break
        idx += 2

    if not rst_positions:
        return frame_data

    # Chunk-határok kiszámítása (iteratív, mert APP3 mérete függ a chunk-szám-tól)
    hdr_bytes = HEADER_K * PAYLOAD_SIZE
    dat_bytes = DATA_K * PAYLOAD_SIZE
    frame_size = len(frame_data)

    for _ in range(3):
        remaining = max(0, frame_size - hdr_bytes)
        n_data = math.ceil(remaining / dat_bytes) if remaining > 0 else 0
        n_total = 1 + n_data
        app3_data_len = 4 + 2 + n_total  # "RST\0"(4) + total(2) + counts
        app3_marker_size = 2 + 2 + app3_data_len  # FF E3 + len(2) + data
        new_size = len(frame_data) + app3_marker_size
        if new_size == frame_size:
            break
        frame_size = new_size

    # RST pozíciók eltolása (APP3 a 2. bájt után kerül be)
    shifted = [p + app3_marker_size for p in rst_positions]

    # Chunk-onkénti RST darabszám
    rst_per_chunk = [0] * n_total
    for pos in shifted:
        if pos < hdr_bytes:
            rst_per_chunk[0] += 1
        else:
            cid = 1 + (pos - hdr_bytes) // dat_bytes
            if cid < n_total:
                rst_per_chunk[cid] += 1

    # APP3 marker összeállítása
    payload = b'RST\0' + struct.pack('>H', len(rst_positions))
    for c in rst_per_chunk:
        payload += bytes([min(c, 255)])

    app3 = b'\xFF\xE3' + struct.pack('>H', len(payload) + 2) + payload
    return frame_data[:2] + app3 + frame_data[2:]


def split_mjpeg_frames(mjpeg_data: bytes) -> list:
    """
    MJPEG formátum: egymást követő JPEG frame-ek (FF D8...FF D9) sorozata.
    Minden frame az FF D8 SOI markertől a következő FF D8-ig tart.
    """
    frames = []
    idx    = 0
    while idx < len(mjpeg_data):
        soi = mjpeg_data.find(b'\xFF\xD8', idx)
        if soi == -1:
            break
        next_soi = mjpeg_data.find(b'\xFF\xD8', soi + 2)
        if next_soi == -1:
            frames.append(inject_rst_table(mjpeg_data[soi:]))
            break
        else:
            frames.append(inject_rst_table(mjpeg_data[soi:next_soi]))
            idx = next_soi
    return frames


# =============================================================================
# Signal Handler - Ctrl+C AZONNALI leállítás
# C++: signal(SIGINT, [](int){ g_stop_flag = true; });
# =============================================================================
def _signal_handler(sig, frame):
    print("\n[*] Ctrl+C elfogva. Leállítás folyamatban...")
    g_stop.set()

signal.signal(signal.SIGINT, _signal_handler)


# =============================================================================
# FŐPROGRAM
# =============================================================================
def main():
    parser = argparse.ArgumentParser(
        description="MJPEG V3 Szimulátor - God-Tier hibajavítással és csomagvesztés szimulációval"
    )
    parser.add_argument("--file", required=True,  help="MJPEG fájl elérési útja")
    parser.add_argument("--port", required=True,  help="Soros port (pl. COM13)")
    parser.add_argument("--baud", type=int, default=921600, help="Baudrate (alap: 921600)")
    parser.add_argument("--loss", type=float, default=0.0,
                        help="Csomagvesztési arány %%-ban (0-100, alap: 0)")
    parser.add_argument("--loop", action="store_true",
                        help="Folyamatosan ismétli a videót")
    args = parser.parse_args()

    # Elveszett csomagok arányának megjelenítése
    if args.loss > 0:
        print(f"[!] CSOMAGVESZTÉS SZIMULÁCIÓ AKTÍV: {args.loss:.1f}%")

    # MJPEG betöltése
    print(f"[*] Fájl betöltése: {args.file}")
    try:
        with open(args.file, "rb") as f:
            mjpeg_data = f.read()
    except FileNotFoundError:
        print(f"[!] HIBA: Fájl nem található: {args.file}")
        sys.exit(1)

    frames = split_mjpeg_frames(mjpeg_data)
    if not frames:
        print("[!] HIBA: Nem találtam JPEG frame-eket a fájlban!")
        sys.exit(1)

    print(f"[*] {len(frames)} képkocka betöltve. Soros port megnyitása: {args.port} @ {args.baud} bps...")

    # Soros port megnyitása
    try:
        tx = SerialTransmitter(args.port, args.baud, args.loss)
    except Exception as e:
        print(f"[!] Soros port hiba: {e}")
        sys.exit(1)

    # Előkészítés
    chunker     = UepChunker(PAYLOAD_SIZE)
    encoder     = PacketEncoder()
    interleaver = Interleaver(group_size=INTERLEAVE_GROUP)

    total_pkts_sent  = 0
    total_pkts_lost  = 0
    total_bytes_sent = 0

    print("[*] Adás elkezdve! (Kilépés: Ctrl+C)")
    print(f"[*] Protokoll: {PACKET_SIZE}B csomagok, {PAYLOAD_SIZE}B hasznos adat")
    print(f"[*] UEP: Fejléc {HEADER_K}+{HEADER_M}={HEADER_K+HEADER_M} | Adat {DATA_K}+{DATA_M}={DATA_K+DATA_M}")

    iteration = 0
    try:
        while not g_stop.is_set():
            iteration += 1
            if args.loop:
                print(f"\n[*] --- Iteráció #{iteration} ---")

            frame_id = 0
            for frame_bytes in frames:
                if g_stop.is_set():
                    break

                chunks = chunker.chunk_frame(frame_bytes)
                stream = interleaver.build_stream(frame_id, chunks, encoder)

                print(f"[>] Frame {frame_id:3d}: {len(frame_bytes):7d} B  |  "
                      f"{len(chunks):4d} chunks  |  {len(stream):6d} csomagok  |  "
                      f"~{len(stream)*PACKET_SIZE/1024:.1f} KB küldendő")

                sent_this_frame = 0
                for pkt in stream:
                    if g_stop.is_set():
                        break
                    ok = tx.send_packet(pkt)
                    total_pkts_sent  += 1
                    total_bytes_sent += len(pkt)
                    if ok:
                        sent_this_frame += 1
                    else:
                        total_pkts_lost += 1

                if not g_stop.is_set():
                    loss_pct = (1 - sent_this_frame / len(stream)) * 100 if stream else 0
                    print(f"    Elküldve: {sent_this_frame}/{len(stream)} csomag  |  "
                          f"Elveszett: {loss_pct:.1f}%")

                frame_id = (frame_id + 1) % 256

            if not args.loop:
                break

    except KeyboardInterrupt:
        print("\n[*] Ctrl+C - Leállítás...")
        g_stop.set()

    # Összefoglaló
    print(f"\n[*] Adás vége.")
    print(f"    Összes csomag: {total_pkts_sent}")
    print(f"    Elveszett:     {total_pkts_lost}  ({total_pkts_lost/max(total_pkts_sent,1)*100:.1f}%)")
    print(f"    Adat:          {total_bytes_sent/1024:.1f} KB")

    tx.close()
    print("[*] Kész. Kilépés.")


if __name__ == "__main__":
    main()
