#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import sys
import threading
import queue
import time
import numpy as np

try:
    import serial
except ImportError:
    print("Hiba: a 'pyserial' csomag nincs telepítve.")
    sys.exit(1)

try:
    import cv2
except ImportError:
    print("Hiba: az 'opencv-python' csomag nincs telepítve.")
    sys.exit(1)

# Protokoll konstansok
PACKET_SIZE = 126
PAYLOAD_SIZE = 120
SYNC_BYTE = 0xFE
TYPE_MJPEG = 0xDD
TYPE_FEC = 0xFF
GROUP_SIZE = 9

# CRC8
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

# DHT Táblák
_DC_LUM_COUNTS = bytes([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0])
_DC_LUM_VALUES = bytes(range(12))
_DC_CHR_COUNTS = bytes([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0])
_DC_CHR_VALUES = bytes(range(12))
_AC_LUM_COUNTS = bytes([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D])
_AC_LUM_VALUES = bytes([
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
])
_AC_CHR_COUNTS = bytes([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77])
_AC_CHR_VALUES = bytes([
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
])

def _build_dht_table(tc, tid, counts, values):
    return bytes([(tc << 4) | tid]) + counts + values

STANDARD_DHT_SEGMENT = b"\xFF\xC4\x01\xA2" + \
    _build_dht_table(0, 0, _DC_LUM_COUNTS, _DC_LUM_VALUES) + \
    _build_dht_table(0, 1, _DC_CHR_COUNTS, _DC_CHR_VALUES) + \
    _build_dht_table(1, 0, _AC_LUM_COUNTS, _AC_LUM_VALUES) + \
    _build_dht_table(1, 1, _AC_CHR_COUNTS, _AC_CHR_VALUES)


class PacketReader:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def read_packets(self):
        chunk = self.ser.read(4096)
        if chunk:
            self.buf.extend(chunk)

        packets = []
        while len(self.buf) >= PACKET_SIZE:
            if self.buf[0] != SYNC_BYTE:
                next_sync = self.buf.find(bytes([SYNC_BYTE]), 1)
                if next_sync == -1:
                    self.buf.clear()
                else:
                    del self.buf[:next_sync]
                continue

            candidate = bytes(self.buf[:PACKET_SIZE])
            if calc_crc8(candidate[:-1]) == candidate[-1]:
                packets.append({
                    "type": candidate[1], "seq": candidate[2],
                    "frame_id": candidate[3], "mask": candidate[4],
                    "payload": candidate[5:5 + PAYLOAD_SIZE]
                })
                del self.buf[:PACKET_SIZE]
            else:
                del self.buf[:1]
        return packets


class FrameAssembler:
    def __init__(self, render_queue):
        self.render_queue = render_queue
        self.pending_group_num = None
        self.pending_slots = [None] * GROUP_SIZE

        self.assembly = bytearray()
        self.frame_open = False
        self.dht_injected = False
        self.last_render_size = 0  # Az optimalizációhoz
        
        self.stats = {
            "frame_id": 0,
            "bytes_rx": 0,
            "lost_total": 0,
            "recovered_total": 0,
            "fec_events": []
        }

    def feed(self, pkt):
        group_num = pkt["seq"] // GROUP_SIZE
        slot = pkt["seq"] % GROUP_SIZE

        if self.pending_group_num is None:
            self.pending_group_num = group_num

        if group_num != self.pending_group_num:
            self._finalize_group()
            self.pending_group_num = group_num

        self.pending_slots[slot] = pkt
        if slot == GROUP_SIZE - 1:
            self._finalize_group()

    def _finalize_group(self):
        slots = self.pending_slots
        fec = slots[GROUP_SIZE - 1]
        missing = [i for i in range(GROUP_SIZE - 1) if slots[i] is None]

        self.stats["fec_events"].clear()

        # FEC hibajavítás
        if missing and fec is not None and len(missing) == 1:
            i = missing[0]
            f_id, mask = fec["frame_id"], fec["mask"]
            payload = bytearray(fec["payload"])
            for j in range(GROUP_SIZE - 1):
                if j == i: continue
                f_id ^= slots[j]["frame_id"]
                mask ^= slots[j]["mask"]
                for k in range(PAYLOAD_SIZE):
                    payload[k] ^= slots[j]["payload"][k]
            slots[i] = {"type": TYPE_MJPEG, "seq": None, "frame_id": f_id, "mask": mask, "payload": bytes(payload)}
            self.stats["recovered_total"] += 1
            self.stats["fec_events"].append("JAVITVA")
            missing = []
        elif missing:
            self.stats["lost_total"] += len(missing)
            self.stats["fec_events"].append(f"ELVESZETT:{len(missing)}")

        for i in range(GROUP_SIZE - 1):
            if slots[i] is not None:
                self._process_data_packet(slots[i])

        self.pending_slots = [None] * GROUP_SIZE
        self.pending_group_num = None
        
        self._push_to_render(complete=False)

    def _process_data_packet(self, pkt):
        current_frame_id = pkt["frame_id"]

        # WATCHDOG: Ha a csomag szerint már új kép jön, de lemaradtunk a kezdetét jelző (SOI) csomagról!
        if self.frame_open and current_frame_id != self.stats["frame_id"]:
            # Rákényszerítjük a régi kép lezárását, nehogy végtelenül nőjön a buffer!
            self._push_to_render(complete=True)
            self.assembly = bytearray(b"\xFF\xD8")
            self.frame_open = True
            self.dht_injected = False
            self.last_render_size = 0
            self.stats["frame_id"] = current_frame_id

        mask, payload = pkt["mask"], pkt["payload"]

        if mask < 120:
            if self.frame_open:
                self.assembly.extend(payload[:mask])
                self._push_to_render(complete=True)
            
            self.assembly = bytearray(b"\xFF\xD8")
            self.assembly.extend(payload[mask:])
            self.frame_open = True
            self.dht_injected = False
            self.last_render_size = 0
            self.stats["frame_id"] = current_frame_id
            self.stats["bytes_rx"] = len(self.assembly)
        else:
            if self.frame_open:
                self.assembly.extend(payload)
                self.stats["bytes_rx"] = len(self.assembly)

        if self.frame_open and not self.dht_injected:
            sos_idx = self.assembly.find(b"\xFF\xDA")
            if sos_idx != -1:
                self.assembly = self.assembly[:sos_idx] + STANDARD_DHT_SEGMENT + self.assembly[sos_idx:]
                self.dht_injected = True
                self.stats["bytes_rx"] = len(self.assembly)

    def _push_to_render(self, complete=False):
        if not self.frame_open or len(self.assembly) < 100:
            return
            
        # OPTIMALIZÁCIÓ: Ha nem teljes a kép, csak akkor zavarjuk az OpenCV-t, 
        # ha legalább 8 KB friss adat jött az előző frissítés óta. 
        # (Ez megakadályozza a CPU 100%-ra pörgését nagy képeknél).
        if not complete:
            if len(self.assembly) - self.last_render_size < 8192:
                return
                
        self.last_render_size = len(self.assembly)

        frame_data = bytes(self.assembly)
        if not complete:
            frame_data += b"\xFF\xD9"

        try:
            self.render_queue.put_nowait({
                "data": frame_data,
                "complete": complete,
                "stats": self.stats.copy()
            })
        except queue.Full:
            pass


def reader_thread_fn(ser, assembler, stop_event):
    reader = PacketReader(ser)
    while not stop_event.is_set():
        try:
            packets = reader.read_packets()
        except Exception:
            time.sleep(0.1)
            continue

        for pkt in packets:
            if pkt["type"] in (TYPE_MJPEG, TYPE_FEC):
                assembler.feed(pkt)
        
        if not packets:
            time.sleep(0.001)


def draw_dashboard(canvas, stats, complete, fps, decode_ok):
    h, w, _ = canvas.shape
    dash_w = 300
    cv2.rectangle(canvas, (w - dash_w, 0), (w, h), (30, 30, 30), -1)
    cv2.line(canvas, (w - dash_w, 0), (w - dash_w, h), (100, 100, 100), 2)
    
    x = w - dash_w + 15
    y = 30
    
    def put_text(text, color=(255, 255, 255), size=0.5, thickness=1):
        nonlocal y
        cv2.putText(canvas, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, size, color, thickness, cv2.LINE_AA)
        y += 25

    put_text("DDEEFF DECODER", color=(0, 255, 255), size=0.7, thickness=2)
    y += 10
    
    if not decode_ok:
        put_text(">> DECODE ERROR <<", color=(0, 0, 255), size=0.6, thickness=2)
    else:
        put_text(f"Status: {'[ KESZ ]' if complete else 'TOLTES...'} ", color=(0, 255, 0) if complete else (0, 165, 255))
        
    put_text(f"Frame ID: {stats.get('frame_id', 0)}")
    put_text(f"Buffer: {stats.get('bytes_rx', 0)} bytes")
    put_text(f"Render FPS: {fps:.1f}")
    
    y += 20
    put_text("--- FEC STATS ---", color=(200, 200, 200))
    put_text(f"Recovered: {stats.get('recovered_total', 0)} csomag", color=(0, 255, 0))
    put_text(f"Lost: {stats.get('lost_total', 0)} csomag", color=(0, 0, 255))
    
    y += 20
    for evt in stats.get("fec_events", []):
        color = (0, 255, 0) if "JAVITVA" in evt else (0, 0, 255)
        put_text(f">> {evt} <<", color=color, size=0.6, thickness=2)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="Soros port (pl. COM5)")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except serial.SerialException as exc:
        print(f"Hiba a port nyitásakor: {exc}")
        sys.exit(1)

    render_queue = queue.Queue(maxsize=3)
    assembler = FrameAssembler(render_queue)
    stop_event = threading.Event()

    t = threading.Thread(target=reader_thread_fn, args=(ser, assembler, stop_event), daemon=True)
    t.start()

    window_name = "DDEEFF Live Stream"
    cv2.namedWindow(window_name, cv2.WINDOW_AUTOSIZE)

    img_h, img_w = 480, 640 
    dash_w = 300
    canvas = np.zeros((img_h, img_w + dash_w, 3), dtype=np.uint8)

    last_time = time.time()
    frames_rendered = 0
    fps = 0.0
    decode_ok = True

    print("Kliens fut. Várakozás a streamre... (Kilépés: Q vagy ESC)")

    try:
        while True:
            try:
                item = render_queue.get(timeout=0.1)
            except queue.Empty:
                if cv2.waitKey(1) & 0xFF in (27, ord('q')):
                    break
                continue

            frame_data = item["data"]
            stats = item["stats"]
            complete = item["complete"]

            arr = np.frombuffer(frame_data, dtype=np.uint8)
            img = cv2.imdecode(arr, cv2.IMREAD_COLOR)

            if img is not None:
                decode_ok = True
                img_h, img_w = img.shape[:2]
                
                if canvas.shape[0] != img_h or canvas.shape[1] != (img_w + dash_w):
                    canvas = np.zeros((img_h, img_w + dash_w, 3), dtype=np.uint8)

                canvas[0:img_h, 0:img_w] = img
            else:
                decode_ok = False

            now = time.time()
            frames_rendered += 1
            if now - last_time >= 1.0:
                fps = frames_rendered / (now - last_time)
                frames_rendered = 0
                last_time = now

            draw_dashboard(canvas, stats, complete, fps, decode_ok)

            cv2.imshow(window_name, canvas)
            if cv2.waitKey(1) & 0xFF in (27, ord('q')):
                break

    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        t.join(timeout=1.0)
        ser.close()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()