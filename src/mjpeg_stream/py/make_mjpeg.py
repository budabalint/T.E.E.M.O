import os
import sys
import io
import argparse
import concurrent.futures
from typing import List, Tuple

try:
    from PIL import Image
except ImportError:
    print(
        "Hiba: A futtatáshoz szükséges a 'Pillow' könyvtár. Telepítés: pip install Pillow",
        file=sys.stderr,
    )
    sys.exit(1)


class ImagePreprocessor:
    """A forrásképek beolvasásáért, átméretezéséért és memóriában tartásáért felelős osztály."""

    def __init__(self, input_dir: str):
        self.input_dir = input_dir

    def get_source_images(self) -> List[str]:
        valid_exts = (".jpg", ".jpeg", ".png", ".bmp", ".webp")
        if not os.path.exists(self.input_dir):
            return []
        files = [
            os.path.join(self.input_dir, f)
            for f in os.listdir(self.input_dir)
            if f.lower().endswith(valid_exts)
        ]
        files.sort()  # ABC-rendbe állítás a sorrend megtartásához
        return files

    @staticmethod
    def _process_single_image(
        file_path: str, resolution: Tuple[int, int]
    ) -> Image.Image:
        """Egyetlen kép feldolgozása (párhuzamosításhoz)."""
        try:
            img = Image.open(file_path)
            if img.mode != "RGB":
                img = img.convert("RGB")
            return img.resize(resolution, Image.Resampling.LANCZOS)
        except Exception as e:
            print(
                f"\n   [Figyelmeztetés] Hibás kép ({file_path}): {e}", file=sys.stderr
            )
            return None

    def load_and_resize_parallel(
        self, resolution: Tuple[int, int]
    ) -> List[Image.Image]:
        """Többszálú képbeolvasás és átméretezés a sebesség növelése érdekében."""
        files = self.get_source_images()
        if not files:
            return []

        print(
            f"-> {len(files)} kép beolvasása és {resolution[0]}x{resolution[1]} méretre skálázása (többszálon)..."
        )
        images = []

        # CPU magok számának megfelelő szál indítása
        with concurrent.futures.ThreadPoolExecutor() as executor:
            results = executor.map(
                lambda f: self._process_single_image(f, resolution), files
            )
            for img in results:
                if img is not None:
                    images.append(img)

        return images


class BitrateEstimator:
    """A memóriában történő tömörítésért és méretbecslésért felelős osztály."""

    @staticmethod
    def strip_tables(frame_bytes: bytes) -> bytes:
        """Hexadecimális szinten eltávolítja a DHT, DQT és APP0 szegmenseket."""
        out = bytearray()
        i = 0
        n = len(frame_bytes)
        while i < n:
            if frame_bytes[i] == 0xFF:
                if i + 1 >= n:
                    out.append(frame_bytes[i])
                    break
                marker = frame_bytes[i + 1]
                if marker == 0xD8:  # SOI
                    out.extend([0xFF, 0xD8])
                    i += 2
                elif marker == 0xD9:  # EOI
                    out.extend([0xFF, 0xD9])
                    i += 2
                elif marker == 0xDA:  # SOS
                    length = (frame_bytes[i + 2] << 8) + frame_bytes[i + 3]
                    out.extend(frame_bytes[i : i + 2 + length])
                    i += 2 + length
                    while i < n - 1:
                        if frame_bytes[i] == 0xFF and frame_bytes[i + 1] == 0xD9:
                            break
                        out.append(frame_bytes[i])
                        i += 1
                else:
                    length = (frame_bytes[i + 2] << 8) + frame_bytes[i + 3]
                    if marker in (0xDB, 0xC4, 0xE0):
                        i += 2 + length
                    else:
                        out.extend(frame_bytes[i : i + 2 + length])
                        i += 2 + length
            else:
                out.append(frame_bytes[i])
                i += 1
        return bytes(out)

    def estimate_total_size(self, images: List[Image.Image], quality: int) -> int:
        """Kiszámolja a leendő rövidített stream teljes méretét bájtokban."""
        if not images:
            return 0
        total_size = 0
        for idx, img in enumerate(images):
            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=quality)
            raw_bytes = bio.getvalue()
            if idx == 0:
                total_size += len(raw_bytes)
            else:
                stripped = self.strip_tables(raw_bytes)
                total_size += len(stripped)
        return total_size


class BinarySearchOptimizer:
    """Bináris kereséssel megkeresi a LEGNAGYOBB minőséget, ami MÉG BELEFÉR a célméretbe."""

    def __init__(self, estimator: BitrateEstimator):
        self.estimator = estimator

    def find_optimal_quality(
        self, images: List[Image.Image], target_size: int
    ) -> Tuple[int, int]:
        low = 1
        high = 100
        best_q = 1
        best_size = 0

        print(
            f"-> Optimális minőség keresése (Szigorú korlát: max {target_size / 1024:.2f} KB)..."
        )
        while low <= high:
            mid = (low + high) // 2
            est_size = self.estimator.estimate_total_size(images, mid)
            diff = est_size - target_size

            print(
                f"   Próba Q={mid:3d} -> Becsült méret: {est_size/1024:7.2f} KB (Eltérés: {diff/1024:+7.2f} KB)"
            )

            if est_size <= target_size:
                best_q = mid
                best_size = est_size
                low = mid + 1
            else:
                high = mid - 1

        if best_size == 0:
            print(
                "   [Figyelmeztetés] A kért méret annyira kicsi, hogy még Q=1 mellett sem érhető el. Q=1 lesz alkalmazva."
            )
            best_q = 1
            best_size = self.estimator.estimate_total_size(images, 1)

        return best_q, best_size


class NativeMjpegPacker:
    """Natív Python alapú MJPEG csomagoló."""

    def __init__(self, output_file: str):
        self.output_file = output_file

    def pack_from_memory(self, images: List[Image.Image], quality: int):
        """Memóriában lévő képek közvetlen MJPEG-be kódolása."""
        print("-> Stream csomagolása és optimalizálása (Natív mód)...")
        output_data = bytearray()

        for idx, img in enumerate(images):
            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=quality)
            raw_bytes = bio.getvalue()

            if idx == 0:
                output_data.extend(raw_bytes)
            else:
                output_data.extend(BitrateEstimator.strip_tables(raw_bytes))

        with open(self.output_file, "wb") as f:
            f.write(output_data)

        print(
            f"-> Kimeneti fájl sikeresen mentve ide: {self.output_file} ({len(output_data)/1024:.2f} KB)"
        )

    def pack_from_disk(self, folder: str):
        """Képek betöltése egy már létező sorszámozott mappából (Legacy mód)."""
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

            if idx == 0:
                output_data.extend(raw_bytes)
            else:
                output_data.extend(BitrateEstimator.strip_tables(raw_bytes))

        with open(self.output_file, "wb") as f:
            f.write(output_data)

        print(
            f"-> Kimeneti fájl sikeresen mentve ide: {self.output_file} ({len(output_data)/1024:.2f} KB)"
        )


def parse_size(size_str: str) -> int:
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
    try:
        res_str = res_str.lower().replace("x", "*")
        w, h = res_str.split("*")
        return int(w), int(h)
    except Exception:
        print(
            "Hiba: A felbontást SzélességxMagasság formátumban add meg (pl. 1920x1080 vagy 1280*720).",
            file=sys.stderr,
        )
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Objektumorientált intelligens Natív MJPEG Stream Generátor"
    )
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

    args = parser.parse_args()
    target_bytes = parse_size(args.target_size) if args.target_size else 0
    resolution = parse_resolution(args.resolution)

    input_folder = args.input
    preprocessor = ImagePreprocessor(input_dir=input_folder)
    source_images = preprocessor.get_source_images()

    # KIVÉTELES ESET: Ha az input mappa üres, de van "images" mappa, akkor használjuk azt bemenetnek!
    if not source_images and os.path.exists("images"):
        print(
            f"-> Nincs adat a '{input_folder}' mappában. Az 'images' mappa tartalmát használom bemenetként..."
        )
        input_folder = "images"
        preprocessor = ImagePreprocessor(input_dir=input_folder)
        source_images = preprocessor.get_source_images()

    if not source_images:
        print(
            f"Hiba: Nincsenek feldolgozható képek sem a '{args.input}', sem az 'images' könyvtárban!",
            file=sys.stderr,
        )
        sys.exit(1)

    packer = NativeMjpegPacker(output_file=args.output)

    # Döntéshozatal: Okos mód (újrakódolás) vs. Nyers összefűzés (Legacy)
    # Ha a felhasználó kért célméretet VAGY átírta a felbontást, kötelező az Okos módot indítani!
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
                f"-> Kiválasztott optimális minőség: Q={optimal_q} (becsült méret: {estimated_size/1024:.2f} KB)"
            )
            packer.pack_from_memory(resized_images, optimal_q)
        else:
            print(f"-> Fix minőség alkalmazása: Q={args.quality}")
            packer.pack_from_memory(resized_images, args.quality)

    else:
        # --- EGYSZERŰ NYERS ÖSSZEFŰZÉS (LEGACY) ---
        # Ide csak akkor lép be, ha nem adtál meg -t paramétert, és csak magában futtatod az images mappára.
        packer.pack_from_disk(input_folder)


if __name__ == "__main__":
    main()
