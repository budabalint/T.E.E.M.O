"""
backend/mjpeg_encoder.py
========================
MJPEG kódoló és csomagoló modul.

Felelősségei:
  - DHT, DQT, APP0 szegmensek eltávolítása (table stripping)
  - PIL Image-ek JPEG-be kódolása (RST intervallum támogatással)
  - MJPEG stream összerakása memóriában
  - Fájlba írás
"""

import io
import os
import sys
from typing import List, Optional, Callable

try:
    from PIL import Image
except ImportError:
    raise ImportError("A 'Pillow' könyvtár szükséges: pip install Pillow")


# ---------------------------------------------------------------------------
# Table Stripper
# ---------------------------------------------------------------------------

class TableStripper:
    """
    Hexadecimális szinten eltávolítja a JPEG redundáns fejléceit
    (Huffman-táblák: DHT/0xC4, Kvantálási táblák: DQT/0xDB, APP0/0xE0).
    Az első képkocka kivételével minden képkockán alkalmazzuk a sávszélesség csökkentéséhez.
    """

    # Eltávolítandó markerek (az első képkocka után)
    STRIP_MARKERS = frozenset([0xC4, 0xDB, 0xE0])

    @classmethod
    def strip(cls, frame_bytes: bytes) -> bytes:
        """
        Eltávolítja a DHT, DQT és APP0 szegmenseket a JPEG adatból.

        Args:
            frame_bytes: Nyers JPEG bájt string

        Returns:
            Lecsupaszított JPEG bájt string (csak a feltétlenül szükséges szegmensek maradnak)
        """
        out = bytearray()
        i = 0
        n = len(frame_bytes)

        while i < n:
            if frame_bytes[i] == 0xFF:
                if i + 1 >= n:
                    out.append(frame_bytes[i])
                    break

                marker = frame_bytes[i + 1]

                if marker == 0xD8:  # SOI — Start of Image
                    out.extend([0xFF, 0xD8])
                    i += 2

                elif marker == 0xD9:  # EOI — End of Image
                    out.extend([0xFF, 0xD9])
                    i += 2

                elif marker == 0xDA:  # SOS — Start of Scan (az összes adat utána jön)
                    length = (frame_bytes[i + 2] << 8) + frame_bytes[i + 3]
                    out.extend(frame_bytes[i: i + 2 + length])
                    i += 2 + length
                    # A scan adatok az EOI-ig
                    while i < n - 1:
                        if frame_bytes[i] == 0xFF and frame_bytes[i + 1] == 0xD9:
                            break
                        out.append(frame_bytes[i])
                        i += 1

                elif marker in cls.STRIP_MARKERS:
                    # DHT / DQT / APP0 → kihagyjuk
                    if i + 3 < n:
                        length = (frame_bytes[i + 2] << 8) + frame_bytes[i + 3]
                        i += 2 + length
                    else:
                        i += 2

                else:
                    # Minden más szegmens (SOF0, DRI, stb.) megtartjuk
                    if i + 3 < n:
                        length = (frame_bytes[i + 2] << 8) + frame_bytes[i + 3]
                        out.extend(frame_bytes[i: i + 2 + length])
                        i += 2 + length
                    else:
                        out.append(frame_bytes[i])
                        i += 1
            else:
                out.append(frame_bytes[i])
                i += 1

        return bytes(out)


# ---------------------------------------------------------------------------
# JPEG kódoló helper
# ---------------------------------------------------------------------------

def encode_jpeg(
    img: Image.Image,
    quality: int,
    rst_interval: int = 0,
) -> bytes:
    """
    PIL Image-et JPEG bájttá kódol, opcionálisan RST markerekkel.

    Args:
        img: RGB PIL Image
        quality: JPEG minőség (1–100)
        rst_interval: Restart interval MCU-ban (0 = kikapcsolt)

    Returns:
        JPEG bájt string
    """
    bio = io.BytesIO()
    save_kwargs = {"format": "JPEG", "quality": quality}
    if rst_interval > 0:
        save_kwargs["restart_marker_blocks"] = rst_interval
    img.save(bio, **save_kwargs)
    return bio.getvalue()


# ---------------------------------------------------------------------------
# Méretbecslő
# ---------------------------------------------------------------------------

class BitrateEstimator:
    """
    Memóriában becsli meg a leendő MJPEG stream méretét
    a Binary Search Optimizer számára.
    """

    def estimate_total_size(
        self,
        images: List[Image.Image],
        quality: int,
        rst_interval: int = 0,
    ) -> int:
        """
        Kiszámolja a leendő rövidített stream teljes méretét bájtokban.

        Args:
            images: PIL Image lista
            quality: JPEG minőség (1–100)
            rst_interval: RST marker intervallum (0 = kikapcsolt)

        Returns:
            Becsült méret bájtban
        """
        if not images:
            return 0

        total_size = 0
        for idx, img in enumerate(images):
            raw_bytes = encode_jpeg(img, quality, rst_interval)
            if idx == 0:
                total_size += len(raw_bytes)
            else:
                stripped = TableStripper.strip(raw_bytes)
                total_size += len(stripped)
        return total_size

    @staticmethod
    def strip_tables(frame_bytes: bytes) -> bytes:
        """Visszafelé-kompatibilis alias (make_mjpeg.py CLI módhoz)."""
        return TableStripper.strip(frame_bytes)


# ---------------------------------------------------------------------------
# MJPEG Packer
# ---------------------------------------------------------------------------

class MjpegPacker:
    """
    PIL Image-ek sorozatát MJPEG fájllá csomagolja.
    Az első képkocka teljes JPEG fejléccel kerül be,
    a többiből eltávolítjuk a redundáns táblákat.
    """

    def __init__(self, output_file: str):
        self.output_file = output_file

    def pack_from_images(
        self,
        images: List[Image.Image],
        quality: int,
        rst_interval: int = 0,
        progress_cb: Optional[Callable[[int, int], None]] = None,
        strip_tables: bool = True,
    ) -> int:
        """
        Memóriában lévő PIL Image-ek MJPEG-be kódolása és fájlba írása.

        Args:
            images: PIL Image lista (már feldolgozott, helyes méretű képek)
            quality: JPEG minőség (1–100)
            rst_interval: RST marker MCU intervallum (0 = kikapcsolt)
            progress_cb: Opcionális callback(kész_db, összes_db)

        Returns:
            Kimeneti fájl mérete bájtban
        """
        print("-> Stream csomagolása és optimalizálása (Natív mód)...")
        output_data = bytearray()
        total = len(images)

        for idx, img in enumerate(images):
            raw_bytes = encode_jpeg(img, quality, rst_interval)

            if idx == 0 or not strip_tables:
                output_data.extend(raw_bytes)
            else:
                output_data.extend(TableStripper.strip(raw_bytes))

            if progress_cb:
                progress_cb(idx + 1, total)

        with open(self.output_file, "wb") as f:
            f.write(output_data)

        size = len(output_data)
        print(
            f"-> Kimeneti fájl sikeresen mentve: {self.output_file} "
            f"({size / 1024:.2f} KB)"
        )
        return size

    def pack_from_disk(self, folder: str, strip_tables: bool = True) -> int:
        """
        Képek betöltése egy már létező sorszámozott mappából (Legacy / raw-stitch mód).
        Újrakódolás nélkül fűzi össze a JPEG fájlokat.

        Returns:
            Kimeneti fájl mérete bájtban
        """
        print("-> Fájlok nyers összefűzése meglévő mappából (újrakódolás nélkül)...")

        files = [f for f in os.listdir(folder) if f.lower().endswith((".jpeg", ".jpg"))]
        files.sort(key=lambda f: int("".join(filter(str.isdigit, f)) or 0))

        if not files:
            print("Hiba: Nincsenek JPEG fájlok a mappában.", file=sys.stderr)
            sys.exit(1)

        output_data = bytearray()
        for idx, filename in enumerate(files):
            with open(os.path.join(folder, filename), "rb") as f:
                raw_bytes = f.read()
            if idx == 0 or not strip_tables:
                output_data.extend(raw_bytes)
            else:
                output_data.extend(TableStripper.strip(raw_bytes))

        with open(self.output_file, "wb") as f:
            f.write(output_data)

        size = len(output_data)
        print(
            f"-> Kimeneti fájl sikeresen mentve: {self.output_file} "
            f"({size / 1024:.2f} KB)"
        )
        return size


# ---------------------------------------------------------------------------
# Visszafelé-kompatibilis aliasok (make_mjpeg.py CLI mód)
# ---------------------------------------------------------------------------

class NativeMjpegPacker(MjpegPacker):
    """Visszafelé-kompatibilis alias az eredeti make_mjpeg.py számára."""

    def pack_from_memory(self, images: List[Image.Image], quality: int, strip_tables: bool = True):
        """Eredeti interfész alias."""
        self.pack_from_images(images, quality, strip_tables=strip_tables)
