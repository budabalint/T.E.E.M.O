#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =============================================================================
# MJPEG Decoder V3 - "God-Tier" Architecture
# C++ TRANSLATION COMMENTS INCLUDED
# =============================================================================
# ARCHITEKTÚRA: 3 szál
#   1. READER SZÁL  → soros portról csomagot olvas (nagyon gyors, soha nem blokkol)
#   2. DECODER SZÁL → batch-ben feldolgoz, FEC dekódol, render queue-ba küld
#   3. GUI SZÁL     → OpenCV ablakokat rajzol (main thread)
# =============================================================================

import sys
import os
import time
import argparse
import threading
import queue
import signal
import ctypes
import numpy as np

# --- stderr elnyomás a libjpeg "Corrupt JPEG data" warningokhoz ---
# Az OPENCV_LOG_LEVEL csak az OpenCV saját logjait nyomja el,
# de a libjpeg közvetlenül stderr-re ír C szinten.
# Windowson a stderr-t /dev/null-ra irányítjuk C-szinten.
# C++: freopen("NUL", "w", stderr);
if sys.platform == 'win32':
    try:
        _kernel32 = ctypes.windll.kernel32
        _INVALID = ctypes.c_void_p(-1).value
        _STD_ERROR_HANDLE = ctypes.c_uint(-12)
        _devnull_handle = _kernel32.CreateFileW(
            "NUL", 0x40000000, 0x00000003, None, 3, 0, None)
        if _devnull_handle != _INVALID:
            _kernel32.SetStdHandle(_STD_ERROR_HANDLE, _devnull_handle)
    except Exception:
        pass  # Ha nem sikerül, nem baj — csak cosmetikus warningok lesznek

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'shared')))
# pyrefly: ignore [missing-import]
from fec_codec import FecCodec

try:
    import cv2
    import serial
except ImportError:
    print("Hiba: pip install pyserial opencv-python")
    sys.exit(1)

# --- Protocol Constants (must match simulator!) ---
PACKET_SIZE  = 127
PAYLOAD_SIZE = 121
SYNC_BYTE    = 0xFE
TYPE_MJPEG   = 0xDD
TYPE_FEC     = 0xFF
HEADER_K, HEADER_M = 10, 10
DATA_K,   DATA_M   = 20, 12

# Globális leállítás
g_stop = threading.Event()

def _sig(sig, frame):
    print("\n[*] Leállítás...")
    g_stop.set()
signal.signal(signal.SIGINT, _sig)


def calc_crc8(data: bytes) -> int:
    # C++: uint8_t calc_crc8(const uint8_t* d, int n)
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


# =============================================================================
# FrameBuffer
# =============================================================================
class FrameBuffer:
    """
    C++: class FrameBuffer { ... };
    Összegyűjti egy frame csomagjait chunk_id szerint.
    """
    _header_codec = FecCodec(HEADER_K, HEADER_M)
    _data_codec   = FecCodec(DATA_K, DATA_M)

    def __init__(self, frame_id: int):
        self.frame_id = frame_id
        # chunk_id -> { pkt_idx -> payload }
        self.chunks: dict[int, dict[int, bytearray]] = {}
        # Már sikeresen dekódolt chunkok cache-e
        self.decoded_chunks: dict[int, bytes] = {}

    def add_packet(self, chunk_id: int, pkt_idx: int, payload: bytes):
        if chunk_id not in self.chunks:
            self.chunks[chunk_id] = {}
        if pkt_idx not in self.chunks[chunk_id]:
            self.chunks[chunk_id][pkt_idx] = bytearray(payload)

    def _decode_one_chunk(self, chunk_id: int) -> tuple:
        """Visszatér: (siker, bytes)"""
        K     = HEADER_K if chunk_id == 0 else DATA_K
        codec = self._header_codec if chunk_id == 0 else self._data_codec

        pkts = self.chunks.get(chunk_id, {})
        indices  = sorted(pkts.keys())
        payloads = [pkts[i] for i in indices]

        if len(indices) < K:
            return False, bytes(K * PAYLOAD_SIZE)

        used_idx  = indices[:K]
        used_pkts = payloads[:K]

        # FAST-PATH: 0..K-1 sorrendben megvan → nincs mátrix inverzió!
        if all(i < K for i in used_idx):
            recovered = [p for _, p in sorted(zip(used_idx, used_pkts))]
        else:
            try:
                recovered = codec.decode(used_pkts, used_idx, PAYLOAD_SIZE)
            except Exception:
                return False, bytes(K * PAYLOAD_SIZE)

        result = bytearray()
        for pkt in recovered:
            result.extend(pkt)
        return True, bytes(result)

    def build_jpeg(self) -> tuple:
        """
        Összerakja a JPEG bájtokat chunk-alapú RST szinkronizációval.
        Az APP3 térkép chunk-onkénti RST darabszámot tartalmaz.
        Ha egy chunk elveszett, pontosan annyi dummy RST markert injektálunk.
        """
        import struct
        stats = {
            "chunks_total":     len(self.chunks),
            "chunks_recovered": 0,
            "chunks_failed":    0,
            "total_lost_pkts":  0,
        }

        if not self.chunks:
            return b'', stats

        max_chunk_id = max(self.chunks.keys())
        total_chunks_expected = max_chunk_id + 1

        # --- 1. PASSZ: Összes elérhető chunk dekódolása ---
        for chunk_id in range(total_chunks_expected):
            K = HEADER_K if chunk_id == 0 else DATA_K
            if chunk_id not in self.decoded_chunks and chunk_id in self.chunks:
                available = len(self.chunks[chunk_id])
                if available >= K:
                    ok, data = self._decode_one_chunk(chunk_id)
                    if ok:
                        self.decoded_chunks[chunk_id] = data

        # --- 2. PASSZ: APP3 RST térkép kiolvasása a fejlécből ---
        contiguous = bytearray()
        for chunk_id in range(total_chunks_expected):
            if chunk_id in self.decoded_chunks:
                contiguous.extend(self.decoded_chunks[chunk_id])
            else:
                break

        rst_per_chunk = None
        if len(contiguous) > 4 and contiguous[:2] == b'\xFF\xD8':
            idx = 2
            while idx < len(contiguous) - 1:
                if contiguous[idx] != 0xFF:
                    break
                marker = contiguous[idx + 1]
                if marker == 0xDA:
                    break
                if idx + 3 >= len(contiguous):
                    break
                length = (contiguous[idx + 2] << 8) + contiguous[idx + 3]
                if marker == 0xE3 and length >= 8:
                    if contiguous[idx+4:idx+8] == b'RST\0':
                        n_chunks_in_table = length - 8
                        rst_per_chunk = []
                        for r in range(n_chunks_in_table):
                            off = idx + 10 + r
                            if off < len(contiguous):
                                rst_per_chunk.append(contiguous[off])
                            else:
                                rst_per_chunk.append(0)
                        break
                idx += 2 + length

        # --- 3. PASSZ: Összefűzés + pontos RST injektálás ---
        acc = bytearray()
        last_rst_val = -1

        for chunk_id in range(total_chunks_expected):
            K = HEADER_K if chunk_id == 0 else DATA_K
            N = (HEADER_K + HEADER_M) if chunk_id == 0 else (DATA_K + DATA_M)

            if chunk_id in self.decoded_chunks:
                data = self.decoded_chunks[chunk_id]
                stats["chunks_recovered"] += 1

                # Utolsó RST marker értékének követése ebben a chunkban
                search_pos = 0
                while True:
                    p = data.find(b'\xFF', search_pos)
                    if p == -1 or p >= len(data) - 1:
                        break
                    m = data[p + 1]
                    if 0xD0 <= m <= 0xD7:
                        last_rst_val = m - 0xD0
                    search_pos = p + 2

                acc.extend(data)
            else:
                # --- ELVESZETT CHUNK: RST INJEKTÁLÁS ---
                stats["chunks_failed"] += 1
                if chunk_id in self.chunks:
                    available = len(self.chunks[chunk_id])
                    stats["total_lost_pkts"] += max(0, N - available)
                else:
                    stats["total_lost_pkts"] += N

                n_missing = 0
                if rst_per_chunk and chunk_id < len(rst_per_chunk):
                    n_missing = rst_per_chunk[chunk_id]

                for i in range(n_missing):
                    rv = (last_rst_val + 1 + i) % 8
                    acc.extend(bytes([0xFF, 0xD0 + rv]))
                if n_missing > 0:
                    last_rst_val = (last_rst_val + n_missing) % 8

        # JPEG lezárása
        if acc:
            acc.extend(b'\xFF\xD9')

        return bytes(acc), stats


# =============================================================================
# SerialReceiver
# =============================================================================
class SerialReceiver:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def read_packets(self) -> list:
        try:
            raw = self.ser.read(8192)
            if raw:
                self.buf.extend(raw)
        except Exception:
            return []

        packets = []
        while len(self.buf) >= PACKET_SIZE:
            if self.buf[0] != SYNC_BYTE:
                nxt = self.buf.find(bytes([SYNC_BYTE]), 1)
                if nxt == -1:
                    self.buf.clear()
                else:
                    del self.buf[:nxt]
                continue

            candidate = bytes(self.buf[:PACKET_SIZE])
            expected_crc = calc_crc8(candidate[:PACKET_SIZE - 1])
            if expected_crc == candidate[PACKET_SIZE - 1]:
                packets.append({
                    "packet_type": candidate[1],
                    "frame_id": candidate[2],
                    "chunk_id": candidate[3],
                    "pkt_idx":  candidate[4],
                    "payload":  candidate[5:5 + PAYLOAD_SIZE],
                })
                del self.buf[:PACKET_SIZE]
            else:
                del self.buf[:1]

        return packets


# =============================================================================
# SZÁL 1: Serial Reader (nagyon gyors, soha nem blokkol)
# =============================================================================
def reader_thread_fn(ser, packet_queue: queue.Queue, stop_event: threading.Event):
    rx = SerialReceiver(ser)
    while not stop_event.is_set():
        packets = rx.read_packets()
        for pkt in packets:
            try:
                packet_queue.put_nowait(pkt)
            except queue.Full:
                pass  # Inkább eldobunk egy csomagot mint blokkolunk
        if not packets:
            time.sleep(0.001)


# =============================================================================
# SZÁL 2: Decoder (batch-ben dolgozik, nem egyenként!)
# =============================================================================
def decoder_thread_fn(packet_queue: queue.Queue, render_queue: queue.Queue, stop_event: threading.Event):
    active_frames: dict[int, FrameBuffer] = {}
    REFRESH_INTERVAL = 0.10  # 100ms = ~10 FPS partial refresh
    last_refresh     = time.monotonic()
    last_fid         = -1

    while not stop_event.is_set():
        # ===============================================================
        # BATCH DRAIN: Egyszerre kiszedünk MINDENT a packet_queue-ból!
        # Ez a KULCS: nem egyenként veszünk ki csomagot, hanem az összes
        # eddig beérkezettet egyszerre feldolgozzuk.
        # ===============================================================
        batch = []
        try:
            # Első elem: blokkol max 10ms-ig (ha nincs semmi, idle loop)
            pkt = packet_queue.get(timeout=0.01)
            batch.append(pkt)
        except queue.Empty:
            pass

        # A maradékot azonnal, nem-blokkolóan vesszük ki
        while True:
            try:
                pkt = packet_queue.get_nowait()
                batch.append(pkt)
            except queue.Empty:
                break

        # Feldolgozzuk a batch-et
        for pkt in batch:
            fid     = pkt["frame_id"]
            cid     = pkt["chunk_id"]
            pkt_idx = pkt["pkt_idx"]
            payload = pkt["payload"]

            # Új frame érkezett → az előzőeket finalizáljuk
            if fid not in active_frames:
                old_fids = [f for f in list(active_frames.keys()) if f != fid]
                for old_fid in old_fids:
                    fb = active_frames[old_fid]
                    jpeg_bytes, stats = fb.build_jpeg()
                    print(f"[DEC] Frame {old_fid} FINAL: {len(jpeg_bytes)}B, "
                          f"chunks={stats['chunks_total']}, "
                          f"ok={stats['chunks_recovered']}, "
                          f"fail={stats['chunks_failed']}")
                    try:
                        render_queue.put_nowait({
                            "data":     jpeg_bytes,
                            "stats":    stats,
                            "frame_id": old_fid,
                            "partial":  False,
                        })
                    except queue.Full:
                        print(f"[DEC] !!! render_queue FULL, frame {old_fid} DROPPED!")
                    del active_frames[old_fid]

                active_frames[fid] = FrameBuffer(fid)
                print(f"[DEC] New frame started: {fid}")

            active_frames[fid].add_packet(cid, pkt_idx, payload)
            last_fid = fid

        # === IDŐZÍTŐ ALAPÚ PARTIAL RENDER (10 FPS) ===
        now = time.monotonic()
        if now - last_refresh >= REFRESH_INTERVAL and last_fid >= 0 and last_fid in active_frames:
            last_refresh = now
            partial_bytes, stats = active_frames[last_fid].build_jpeg()
            if partial_bytes:
                try:
                    render_queue.put_nowait({
                        "data":     partial_bytes,
                        "stats":    stats,
                        "frame_id": last_fid,
                        "partial":  True,
                    })
                except queue.Full:
                    pass


# =============================================================================
# Dashboard rajzoló
# =============================================================================
def draw_dashboard(stats: dict, fps: float, decode_ok: bool, frame_id: int, partial: bool) -> np.ndarray:
    W, H = 360, 320
    canvas = np.zeros((H, W, 3), dtype=np.uint8)

    for row in range(H):
        a = row / H
        canvas[row, :] = [int(25 * (1 - a) + 10 * a)] * 3

    x, y = 12, 0

    def pt(text, color=(220, 220, 220), sz=0.47, bold=False):
        nonlocal y
        y += 28
        cv2.putText(canvas, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX,
                    sz, color, 2 if bold else 1, cv2.LINE_AA)

    pt("V3  GOD-TIER  DECODER", color=(0, 230, 230), sz=0.58, bold=True)
    cv2.line(canvas, (x, y + 6), (W - x, y + 6), (0, 180, 180), 1)

    if not decode_ok and not partial:
        pt(">> DECODE ERROR <<", color=(0, 60, 255), sz=0.55, bold=True)
    elif partial:
        pt("[ PARTIAL  - LIVE ]", color=(0, 165, 255), sz=0.5, bold=True)
    else:
        pt("[ FRAME COMPLETE  ]", color=(0, 220, 80), sz=0.5, bold=True)

    pt(f"Frame ID : {frame_id}")
    pt(f"FPS      : {fps:5.1f}")
    y += 8
    cv2.line(canvas, (x, y), (W - x, y), (80, 80, 80), 1)
    pt("GF(256)  UEP  STATS", color=(180, 180, 180), sz=0.44, bold=True)
    pt(f"Chunks Rx       : {stats.get('chunks_total', 0)}")
    pt(f"Chunks Recovered: {stats.get('chunks_recovered', 0)}", color=(0, 210, 80))
    pt(f"Chunks Failed   : {stats.get('chunks_failed', 0)}",
       color=(0, 80, 255) if stats.get('chunks_failed', 0) else (0, 210, 80))
    pt(f"Lost Packets    : {stats.get('total_lost_pkts', 0)}",
       color=(0, 140, 255) if stats.get('total_lost_pkts', 0) else (0, 210, 80))

    if stats.get('chunks_failed', 0) > 0:
        y += 4
        pt("! ZERO-PADDING INJECTED !", color=(0, 140, 255), sz=0.46, bold=True)

    return canvas


# =============================================================================
# MAIN (GUI szál)
# =============================================================================
def main():
    parser = argparse.ArgumentParser(description="MJPEG V3 Dekóder - God-Tier")
    parser.add_argument("--port", required=True, help="Soros port (pl. COM13)")
    parser.add_argument("--baud", type=int, default=921600)
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except serial.SerialException as e:
        print(f"[!] Port hiba: {e}")
        sys.exit(1)

    render_queue = queue.Queue(maxsize=10)
    packet_queue = queue.Queue(maxsize=50000)  # Bőven elég 5 frame-nyi csomagnak
    stop_event   = g_stop

    t_reader = threading.Thread(target=reader_thread_fn, args=(ser, packet_queue, stop_event), daemon=True)
    t_decoder = threading.Thread(target=decoder_thread_fn, args=(packet_queue, render_queue, stop_event), daemon=True)
    t_reader.start()
    t_decoder.start()

    WIN_VIDEO = "V3 Video Stream"
    WIN_DASH  = "V3 Dashboard"
    cv2.namedWindow(WIN_VIDEO, cv2.WINDOW_NORMAL)
    cv2.namedWindow(WIN_DASH,  cv2.WINDOW_AUTOSIZE)
    cv2.resizeWindow(WIN_VIDEO, 800, 600)

    last_good_img   = None
    fps             = 0.0
    frames_rendered = 0
    last_fps_time   = time.monotonic()
    decode_ok       = True
    cur_stats       = {}
    cur_fid         = 0
    cur_partial     = False

    print(f"[*] Dekóder indul. Port: {args.port} @ {args.baud}")
    print("[*] Várakozás adatokra... (Kilépés: Q vagy ablak bezárása)")

    global_jpeg_header = b''

    def extract_full_header(data: bytes) -> bytes:
        """Kinyeri a JPEG headert, de KIHAGYJA az APP3 markert (RST térkép)."""
        result = bytearray(b'\xFF\xD8')
        idx = 2
        while idx < len(data) - 1:
            if data[idx] != 0xFF:
                break
            marker = data[idx+1]
            if marker == 0xDA:  # SOS
                if idx + 3 < len(data):
                    length = (data[idx+2] << 8) + data[idx+3]
                    result.extend(data[idx:idx + 2 + length])
                    return bytes(result)
                break
            if idx + 3 >= len(data):
                break
            length = (data[idx+2] << 8) + data[idx+3]
            if marker != 0xE3:  # APP3-at KIHAGYJUK!
                result.extend(data[idx:idx + 2 + length])
            idx += 2 + length
        return b''

    def is_abbreviated(data: bytes) -> bool:
        """True ha a JPEG-ből hiányoznak a DQT táblák (abbreviated format)."""
        if not data.startswith(b'\xFF\xD8'):
            return False
        idx = 2
        while idx < min(len(data), 4000):
            if data[idx] != 0xFF:
                break
            marker = data[idx+1]
            if marker == 0xDB:
                return False  # Van DQT → NEM abbreviated
            if marker == 0xDA:
                break  # SOS → vége
            if idx + 3 >= len(data):
                break
            length = (data[idx+2] << 8) + data[idx+3]
            idx += 2 + length
        return True

    while not stop_event.is_set():
        # Ablak bezárás detektálása
        try:
            v_vis = cv2.getWindowProperty(WIN_VIDEO, cv2.WND_PROP_VISIBLE)
            d_vis = cv2.getWindowProperty(WIN_DASH,  cv2.WND_PROP_VISIBLE)
            if v_vis < 1 or d_vis < 1:
                print("[*] Ablak bezárva.")
                break
        except cv2.error:
            break

        # Képkocka megjelenítése
        got_something = False
        while True:
            try:
                item = render_queue.get_nowait()
                cur_fid     = item["frame_id"]
                cur_stats   = item["stats"]
                cur_partial = item.get("partial", False)
                data        = item["data"]

                if data:
                    # TELJES HEADER KINYERÉSE (FRAME 0)
                    if cur_fid == 0 and not global_jpeg_header and len(data) > 1024:
                        if data.startswith(b'\xFF\xD8'):
                            global_jpeg_header = extract_full_header(data)
                            print(f"[GUI] Extracted FULL Header ({len(global_jpeg_header)} bytes) from Frame 0")

                    # HEADER BEFECSKENDEZÉSE / PÓTLÁSA (FRAME > 0)
                    if cur_fid > 0 and global_jpeg_header and len(data) > 0:
                        if data.startswith(b'\xFF\xD8') and is_abbreviated(data):
                            # Megkeressük az SOS-t az abbreviated adatban
                            sos_idx = data.find(b'\xFF\xDA')
                            if sos_idx != -1:
                                sos_len = (data[sos_idx+2] << 8) + data[sos_idx+3]
                                scan_data = data[sos_idx + 2 + sos_len:]
                                data = global_jpeg_header + scan_data
                        elif not data.startswith(b'\xFF\xD8'):
                            # Chunk 0 teljesen elveszett
                            data = global_jpeg_header + data

                    arr = np.frombuffer(data, dtype=np.uint8)
                    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
                    
                    if img is not None:
                        decode_ok     = True
                        last_good_img = img.copy()  # Elmentjük, de NEM használjuk inpaintingre
                        frames_rendered += 1
                    else:
                        if not cur_partial:
                            decode_ok = False

                got_something = True
            except queue.Empty:
                break

        # FPS
        now = time.monotonic()
        if now - last_fps_time >= 1.0:
            fps             = frames_rendered / (now - last_fps_time)
            frames_rendered = 0
            last_fps_time   = now

        # Dashboard
        dash = draw_dashboard(cur_stats, fps, decode_ok, cur_fid, cur_partial)
        cv2.imshow(WIN_DASH, dash)

        # Videó
        if last_good_img is not None:
            cv2.imshow(WIN_VIDEO, last_good_img)

        key = cv2.waitKey(10) & 0xFF
        if key in (27, ord('q'), ord('Q')):
            break

    stop_event.set()
    t_reader.join(timeout=1.0)
    t_decoder.join(timeout=1.0)
    try:
        ser.close()
    except Exception:
        pass
    cv2.destroyAllWindows()
    print("[*] Kész.")


if __name__ == "__main__":
    main()
