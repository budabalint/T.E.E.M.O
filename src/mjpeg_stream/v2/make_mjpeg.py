"""
make_mjpeg.py
=============
Objektumorientált intelligens Natív MJPEG Stream Generátor — Orchestrátor

Két üzemmód:
  1. CLI mód  : Hagyományos parancssori argumentumok (--input, --output, stb.)
  2. JSON mód : --json <projekt.json> — GUI által generált projektfájl feldolgozása

A tényleges képfeldolgozás és kódolás a backend/ csomagban van megvalósítva.
"""

import os
import sys
import json
import argparse
from typing import Tuple

# --- Backend importok ---
from backend.image_processor import ImagePreprocessor, ImageProcessor, FrameSpec, LogoItem
from backend.mjpeg_encoder import BitrateEstimator, MjpegPacker, NativeMjpegPacker
from backend.size_optimizer import BinarySearchOptimizer


# ---------------------------------------------------------------------------
# Segédfüggvények (CLI szintaxis elemzés)
# ---------------------------------------------------------------------------

def parse_size(size_str: str) -> int:
    """Méretsztringet bájtokra alakít. Pl.: '500K' → 512000, '6M' → 6291456"""
    if not size_str:
        return 0
    size_str = size_str.upper().strip()
    try:
        if size_str.endswith("K"):
            return int(float(size_str[:-1]) * 1024)
        elif size_str.endswith("M"):
            return int(float(size_str[:-1]) * 1024 * 1024)
        else:
            return int(size_str)
    except ValueError:
        print(f"Hiba: Érvénytelen méretformátum: '{size_str}'.", file=sys.stderr)
        sys.exit(1)


def parse_resolution(res_str: str) -> Tuple[int, int]:
    """Felbontás sztringet (width, height) tuple-ra alakít. Pl.: '1280x720'"""
    try:
        res_str = res_str.lower().replace("x", "*")
        w, h = res_str.split("*")
        return int(w), int(h)
    except Exception:
        print(
            "Hiba: A felbontást SzélességxMagasság formátumban add meg "
            "(pl. 1920x1080 vagy 1280*720).",
            file=sys.stderr,
        )
        sys.exit(1)


# ---------------------------------------------------------------------------
# JSON mód
# ---------------------------------------------------------------------------

def run_json_mode(json_path: str):
    """
    GUI által generált projektfájl (JSON) feldolgozása és MJPEG exportálása.

    A JSON struktúra:
    {
      "version": 1,
      "resolution": [1280, 720],
      "aspect_mode": "crop",        // "crop" | "letterbox" | "blur"
      "target_size_bytes": 3145728, // 0 = automatikus (fix quality=85)
      "quality": 85,                // Csak ha target_size_bytes == 0
      "rst_points": 0,              // 0 = RST kikapcsolt
      "output_file": "output.mjpeg",
      "frames": [
        {
          "source_path": "...",
          "logos": [
            { "file_path": "...", "x": 0.85, "y": 0.90, "width": 0.12, "height": 0.08 }
          ]
        }
      ]
    }
    """
    print(f"-> JSON módú feldolgozás: {json_path}")

    try:
        with open(json_path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
    except Exception as e:
        print(f"Hiba: A JSON fájl nem olvasható: {e}", file=sys.stderr)
        sys.exit(1)

    # Paraméterek kinyerése
    resolution = tuple(cfg.get("resolution", [1280, 720]))
    aspect_mode = cfg.get("aspect_mode", "crop")
    target_size = cfg.get("target_size_bytes", 0)
    fix_quality = cfg.get("quality", 85)
    rst_points = cfg.get("rst_points", 0)
    output_file = cfg.get("output_file", "output.mjpeg")
    standard_mjpeg = cfg.get("standard_mjpeg", False)

    # Frame specifikációk
    frame_dicts = cfg.get("frames", [])
    if not frame_dicts:
        print("Hiba: A JSON projektben nincsenek képkockák (frames lista üres)!", file=sys.stderr)
        sys.exit(1)

    frame_specs = [FrameSpec.from_dict(fd) for fd in frame_dicts]
    print(f"-> {len(frame_specs)} képkocka lesz feldolgozva.")
    print(f"-> Felbontás: {resolution[0]}x{resolution[1]}, Illesztés: {aspect_mode}")

    processor = ImageProcessor(
        resolution=resolution,
        rst_points=rst_points,
    )

    # RST intervallum kiszámítása
    rst_interval = processor.get_rst_interval(resolution[0], resolution[1])
    if rst_interval > 0:
        print(f"-> RST Marker aktív: {rst_points} pont/kép -> intervallum = {rst_interval} MCU")
    else:
        print("-> RST Marker: kikapcsolt")

    # Képek feldolgozása (párhuzamosan)
    def on_progress(done, total):
        pct = int(done / total * 100)
        print(f"   Feldolgozás: {done}/{total} ({pct}%)", end="\r")

    print("-> Képek beolvasása és feldolgozása...")
    images = processor.process_frames_parallel(frame_specs, progress_cb=on_progress)
    print()  # newline az \r után

    # Szűrjük a None értékeket
    images = [img for img in images if img is not None]
    if not images:
        print("Hiba: Egyetlen képet sem sikerült feldolgozni!", file=sys.stderr)
        sys.exit(1)

    print(f"-> {len(images)} kép sikeresen feldolgozva.")

    # Packer inicializálása
    packer = MjpegPacker(output_file=output_file)

    if target_size > 0:
        # Optimális minőség keresése (Binary Search)
        estimator = BitrateEstimator()
        optimizer = BinarySearchOptimizer(estimator)

        def on_opt_progress(step, quality, est_size):
            # GUI a stdout-ot olvassa, így strukturált üzenetet küldünk
            print(f"PROGRESS:OPT:{step}:{quality}:{est_size}", flush=True)

        optimal_q, est_size = optimizer.find_optimal_quality(
            images, target_size, rst_interval, progress_cb=on_opt_progress
        )
        print(
            f"-> Kiválasztott optimális minőség: Q={optimal_q} "
            f"(becsült méret: {est_size/1024:.2f} KB)"
        )

        def on_pack_progress(done, total):
            print(f"PROGRESS:PACK:{done}:{total}", flush=True)

        packer.pack_from_images(images, optimal_q, rst_interval, progress_cb=on_pack_progress, strip_tables=not standard_mjpeg)
    else:
        # Fix minőség
        print(f"-> Fix minőség alkalmazása: Q={fix_quality}")

        def on_pack_progress(done, total):
            print(f"PROGRESS:PACK:{done}:{total}", flush=True)

        packer.pack_from_images(images, fix_quality, rst_interval, progress_cb=on_pack_progress, strip_tables=not standard_mjpeg)

    print(f"DONE:{output_file}", flush=True)


# ---------------------------------------------------------------------------
# CLI mód (eredeti viselkedés megőrzve)
# ---------------------------------------------------------------------------

def run_cli_mode(args):
    """Hagyományos CLI mód — az eredeti make_mjpeg.py viselkedése."""
    target_bytes = parse_size(args.target_size) if args.target_size else 0
    resolution = parse_resolution(args.resolution)

    input_folder = args.input
    preprocessor = ImagePreprocessor(input_dir=input_folder)
    source_images = preprocessor.get_source_images()

    # Fallback: ha az input mappa üres, de van "images" mappa
    if not source_images and os.path.exists("images"):
        print(
            f"-> Nincs adat a '{input_folder}' mappában. "
            "Az 'images' mappa tartalmát használom bemenetként..."
        )
        input_folder = "images"
        preprocessor = ImagePreprocessor(input_dir=input_folder)
        source_images = preprocessor.get_source_images()

    if not source_images:
        print(
            f"Hiba: Nincsenek feldolgozható képek sem a '{args.input}', "
            "sem az 'images' könyvtárban!",
            file=sys.stderr,
        )
        sys.exit(1)

    packer = NativeMjpegPacker(output_file=args.output)

    # Okos mód vs. nyers összefűzés döntés
    needs_smart_mode = (
        (target_bytes > 0)
        or (args.resolution != "1280x720")
        or (input_folder != "images")
    )
    if args.raw_stitch:
        needs_smart_mode = False

    if needs_smart_mode:
        # --- OKOS / RATE-CONTROL MÓD ---
        resized_images = preprocessor.load_and_resize_parallel(resolution)

        if not resized_images:
            print("Hiba: Nem sikerült feldolgozni a bemeneti képeket.", file=sys.stderr)
            sys.exit(1)

        if target_bytes > 0:
            estimator = BitrateEstimator()
            optimizer = BinarySearchOptimizer(estimator)
            optimal_q, estimated_size = optimizer.find_optimal_quality(
                resized_images, target_bytes
            )
            print(
                f"-> Kiválasztott optimális minőség: Q={optimal_q} "
                f"(becsült méret: {estimated_size/1024:.2f} KB)"
            )
            packer.pack_from_memory(resized_images, optimal_q, strip_tables=not args.standard)
        else:
            print(f"-> Fix minőség alkalmazása: Q={args.quality}")
            packer.pack_from_memory(resized_images, args.quality, strip_tables=not args.standard)

    else:
        # --- EGYSZERŰ NYERS ÖSSZEFŰZÉS (LEGACY) ---
        packer.pack_from_disk(input_folder, strip_tables=not args.standard)


# ---------------------------------------------------------------------------
# Belépési pont
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Objektumorientált intelligens Natív MJPEG Stream Generátor"
    )

    # JSON mód (GUI által meghívva)
    parser.add_argument(
        "--json", "-j",
        type=str,
        default=None,
        help="GUI projektfájl (JSON) elérési útja — JSON módot aktivál",
    )

    # CLI mód (hagyományos)
    parser.add_argument(
        "--input", "-i", default="input", help="Forrásképeket tartalmazó mappa"
    )
    parser.add_argument(
        "--output", "-o", default="stream_abbreviated.mjpeg", help="Kimeneti fájl"
    )
    parser.add_argument(
        "--target-size", "-t", type=str, help="Szigorú célméret (pl. '500K', '6M')"
    )
    parser.add_argument(
        "--quality", "-q", type=int, default=85, help="Fix JPEG minőség (1-100)"
    )
    parser.add_argument(
        "--resolution",
        "-r",
        type=str,
        default="1280x720",
        help="Kimeneti felbontás (pl. 1280x720)",
    )
    parser.add_argument(
        "--raw-stitch",
        action="store_true",
        help="Kihagyja a tömörítést, csak nyersen összefűz",
    )
    parser.add_argument(
        "--standard",
        action="store_true",
        help="Standard MJPEG generálása (DHT táblák megtartása minden képben, videólejátszókhoz)",
    )

    args = parser.parse_args()

    if args.json:
        run_json_mode(args.json)
    else:
        run_cli_mode(args)


if __name__ == "__main__":
    main()
