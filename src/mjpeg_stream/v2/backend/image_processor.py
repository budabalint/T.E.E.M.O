"""
backend/image_processor.py
==========================
Képfeldolgozó modul az MJPEG Studio számára.

Felelősségei:
  - Torzításmentes átméretezés (crop / letterbox / blur)
  - Logók (PNG alfa-csatornás) ráégetése
  - Restart Marker (RST) intervallum kezelése
  - JSON projektfájlból való kép-pipeline futtatása
"""

import os
import io
import concurrent.futures
from typing import List, Tuple, Optional, Callable, Dict, Any

try:
    from PIL import Image, ImageFilter
except ImportError:
    raise ImportError("A 'Pillow' könyvtár szükséges: pip install Pillow")


# ---------------------------------------------------------------------------
# Adatstruktúrák
# ---------------------------------------------------------------------------

class LogoItem:
    """Egy logó leírója normalizált (0.0–1.0) koordinátákkal."""

    def __init__(
        self,
        file_path: str,
        x: float = 0.0,
        y: float = 0.0,
        width: float = 0.1,
        height: float = 0.1,
        rotation: float = 0.0,
    ):
        self.file_path = file_path
        self.x = x          # 0.0–1.0, bal széltől
        self.y = y          # 0.0–1.0, tetőtől
        self.width = width   # 0.0–1.0, kép szélességéhez viszonyítva
        self.height = height # 0.0–1.0, kép magasságához viszonyítva
        self.rotation = rotation # Forgatás fokban

    def to_dict(self) -> Dict[str, Any]:
        return {
            "file_path": self.file_path,
            "x": self.x,
            "y": self.y,
            "width": self.width,
            "height": self.height,
            "rotation": self.rotation,
        }

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "LogoItem":
        return LogoItem(
            file_path=d["file_path"],
            x=d.get("x", 0.0),
            y=d.get("y", 0.0),
            width=d.get("width", 0.1),
            height=d.get("height", 0.1),
            rotation=d.get("rotation", 0.0),
        )


class FrameSpec:
    """Egy képkocka teljes specifikációja a JSON projektből."""

    def __init__(self, source_path: str, aspect_mode: str = "crop", frame_rotation: int = 0, logos: Optional[List[LogoItem]] = None):
        self.source_path = source_path
        self.aspect_mode = aspect_mode
        self.frame_rotation = frame_rotation
        self.logos: List[LogoItem] = logos or []

    def to_dict(self) -> Dict[str, Any]:
        return {
            "source_path": self.source_path,
            "aspect_mode": self.aspect_mode,
            "frame_rotation": self.frame_rotation,
            "logos": [logo.to_dict() for logo in self.logos],
        }

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "FrameSpec":
        logos = [LogoItem.from_dict(ld) for ld in d.get("logos", [])]
        return FrameSpec(
            source_path=d["source_path"],
            aspect_mode=d.get("aspect_mode", "crop"),
            frame_rotation=d.get("frame_rotation", 0),
            logos=logos
        )


# ---------------------------------------------------------------------------
# Segédfüggvények
# ---------------------------------------------------------------------------

def _load_logo_cached(file_path: str, _cache: Dict[str, Image.Image] = {}) -> Optional[Image.Image]:
    """Logo betöltése gyorsítótárral (RGBA módban)."""
    if file_path not in _cache:
        try:
            logo = Image.open(file_path).convert("RGBA")
            _cache[file_path] = logo
        except Exception as e:
            print(f"  [Figyelmeztetés] Logó betöltési hiba ({file_path}): {e}")
            _cache[file_path] = None
    return _cache[file_path]


def _apply_logos(
    base_img: Image.Image,
    logos: List[LogoItem],
    target_w: int,
    target_h: int,
) -> Image.Image:
    """Logók ráégetése a képre alfa-csatorna figyelembevételével."""
    if not logos:
        return base_img

    # RGBA-ba konvertálunk, hogy az alfa compositing helyes legyen
    result = base_img.convert("RGBA")

    for logo_item in logos:
        logo_img = _load_logo_cached(logo_item.file_path)
        if logo_img is None:
            continue

        # Normalizált koordinátákból abszolút pixelré
        logo_x = int(logo_item.x * target_w)
        logo_y = int(logo_item.y * target_h)
        logo_w = max(1, int(logo_item.width * target_w))
        logo_aspect = logo_img.width / logo_img.height if logo_img.height != 0 else 1.0
        logo_h = max(1, int(logo_w / logo_aspect))

        # Logó átméretezése a kért méretre
        resized_logo = logo_img.resize((logo_w, logo_h), Image.Resampling.LANCZOS)
        
        # Logó forgatása (ha be van állítva)
        if logo_item.rotation != 0.0:
            # expand=True biztosítja, hogy ne vágódjon le a sarka
            resized_logo = resized_logo.rotate(-logo_item.rotation, resample=Image.Resampling.BICUBIC, expand=True)
            # Újra kell számolni a paste_x, paste_y-t hogy a középpont megmaradjon
            logo_x = logo_x - (resized_logo.width - logo_w) // 2
            logo_y = logo_y - (resized_logo.height - logo_h) // 2

        # Compositing: paste a logó alfa-csatornájával mint mask
        result.paste(resized_logo, (logo_x, logo_y), resized_logo)

    # Visszakonvertálunk RGB-re (JPEG nem tud RGBA-t)
    return result.convert("RGB")


def _resize_crop(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """Kitölti a célt, a kilógó széleket középre igazítva levágja."""
    src_w, src_h = img.size
    target_ratio = target_w / target_h
    src_ratio = src_w / src_h

    if src_ratio > target_ratio:
        # Kép szélesebb → magasság alapján scale-elünk, széleket vágjuk
        scale_h = target_h
        scale_w = int(src_w * target_h / src_h)
    else:
        # Kép magasabb → szélesség alapján scale-elünk, tetőt/aljt vágjuk
        scale_w = target_w
        scale_h = int(src_h * target_w / src_w)

    scaled = img.resize((scale_w, scale_h), Image.Resampling.LANCZOS)
    left = (scale_w - target_w) // 2
    top = (scale_h - target_h) // 2
    return scaled.crop((left, top, left + target_w, top + target_h))


def _resize_letterbox(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """A kép teljes egésze látszik, fekete sávokkal töltjük ki a maradékot."""
    img.thumbnail((target_w, target_h), Image.Resampling.LANCZOS)
    result = Image.new("RGB", (target_w, target_h), (0, 0, 0))
    paste_x = (target_w - img.width) // 2
    paste_y = (target_h - img.height) // 2
    result.paste(img, (paste_x, paste_y))
    return result


def _resize_blur(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """Felnagyított, elhomályosított háttér a fekete sávok helyett."""
    # Háttér: teljesen kitölti a célt, erősen elmosva
    bg = img.resize((target_w, target_h), Image.Resampling.LANCZOS)
    bg = bg.filter(ImageFilter.GaussianBlur(radius=20))

    # Előtér: letterbox-szerű, de az eredeti kép
    fg = img.copy()
    fg.thumbnail((target_w, target_h), Image.Resampling.LANCZOS)
    paste_x = (target_w - fg.width) // 2
    paste_y = (target_h - fg.height) // 2
    bg.paste(fg, (paste_x, paste_y))
    return bg


RESIZE_MODES = {
    "crop": _resize_crop,
    "letterbox": _resize_letterbox,
    "blur": _resize_blur,
}


# ---------------------------------------------------------------------------
# Fő képfeldolgozó osztály
# ---------------------------------------------------------------------------

class ImageProcessor:
    """
    Képek teljes feldolgozó pipeline-ja:
      1. Beolvasás
      2. Torzításmentes átméretezés (crop/letterbox/blur)
      3. Logók ráégetése
      4. PIL Image objektum visszaadása (a Packer majd JPEG-be kódolja)
    """

    def __init__(
        self,
        resolution: Tuple[int, int],
        rst_points: int = 0,
    ):
        """
        Args:
            resolution: (szélesség, magasság) célméret
            rst_points: újraindítási pontok száma képenként (0 = kikapcsolva)
        """
        self.resolution = resolution
        self.rst_points = rst_points

    def process_frame(
        self,
        source_path: str,
        aspect_mode: str = "crop",
        frame_rotation: int = 0,
        logos: Optional[List[LogoItem]] = None,
    ) -> Optional[Image.Image]:
        """
        Egyetlen képkocka teljes feldolgozása.

        Returns:
            RGB PIL Image, vagy None hiba esetén.
        """
        try:
            img = Image.open(source_path)
            if img.mode != "RGB":
                img = img.convert("RGB")
                
            # Fő kép elforgatása (ha szükséges)
            if frame_rotation == 90:
                img = img.transpose(Image.Transpose.ROTATE_270) # Óramutató járásával megegyező
            elif frame_rotation == 180:
                img = img.transpose(Image.Transpose.ROTATE_180)
            elif frame_rotation == 270:
                img = img.transpose(Image.Transpose.ROTATE_90)
                
        except Exception as e:
            print(f"  [Figyelmeztetés] Kép beolvasási hiba ({source_path}): {e}")
            return None

        # 1. Torzításmentes átméretezés
        target_w, target_h = self.resolution
        mode = aspect_mode if aspect_mode in RESIZE_MODES else "crop"
        resize_fn = RESIZE_MODES[mode]
        img = resize_fn(img, target_w, target_h)

        # 2. Logók ráégetése
        if logos:
            img = _apply_logos(img, logos, target_w, target_h)

        return img

    def process_frames_parallel(
        self,
        frame_specs: List[FrameSpec],
        progress_cb: Optional[Callable[[int, int], None]] = None,
    ) -> List[Optional[Image.Image]]:
        """
        Több képkocka párhuzamos feldolgozása.

        Args:
            frame_specs: FrameSpec lista (sorrend megmarad)
            progress_cb: Opcionális callback(kész_db, összes_db)

        Returns:
            PIL Image lista (None értékekkel a hibásaknál)
        """
        total = len(frame_specs)
        results = [None] * total
        done = [0]

        def _process(idx_spec):
            idx, spec = idx_spec
            img = self.process_frame(spec.source_path, spec.aspect_mode, spec.frame_rotation, spec.logos)
            done[0] += 1
            if progress_cb:
                progress_cb(done[0], total)
            return idx, img

        with concurrent.futures.ThreadPoolExecutor() as executor:
            for idx, img in executor.map(_process, enumerate(frame_specs)):
                results[idx] = img

        return results

    def get_rst_interval(self, img_width: int, img_height: int) -> int:
        """
        Kiszámolja a Pillow-nak átadandó restart_interval értéket MCU-ban.

        A JPEG MCU blokkmérete YCbCr 4:2:0 esetén 16x16 pixel.
        Ha rst_points == 0 → 0 (kikapcsolva)
        Ha rst_points > 0 → total_mcu / (rst_points + 1)

        Returns:
            MCU blokkok száma restart markerenként (0 = kikapcsolt)
        """
        if self.rst_points <= 0:
            return 0

        mcu_w = (img_width + 15) // 16
        mcu_h = (img_height + 15) // 16
        total_mcu = mcu_w * mcu_h

        interval = max(1, total_mcu // (self.rst_points + 1))
        return interval


# ---------------------------------------------------------------------------
# Régi ImagePreprocessor kompatibilitási alias (CLI mód számára)
# ---------------------------------------------------------------------------

class ImagePreprocessor:
    """
    Visszafelé-kompatibilis wrapper az eredeti make_mjpeg.py CLI módhoz.
    Az új GUI kód az ImageProcessor osztályt használja.
    """

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
        files.sort()
        return files

    @staticmethod
    def _process_single_image(
        file_path: str, resolution: Tuple[int, int]
    ) -> Optional[Image.Image]:
        """Egyetlen kép feldolgozása (párhuzamosításhoz)."""
        try:
            img = Image.open(file_path)
            if img.mode != "RGB":
                img = img.convert("RGB")
            return img.resize(resolution, Image.Resampling.LANCZOS)
        except Exception as e:
            print(f"\n   [Figyelmeztetés] Hibás kép ({file_path}): {e}")
            return None

    def load_and_resize_parallel(
        self, resolution: Tuple[int, int]
    ) -> List[Image.Image]:
        """Többszálú képbeolvasás és átméretezés."""
        files = self.get_source_images()
        if not files:
            return []

        print(
            f"-> {len(files)} kép beolvasása és {resolution[0]}x{resolution[1]} "
            f"méretre skálázása (többszálon)..."
        )
        images = []
        with concurrent.futures.ThreadPoolExecutor() as executor:
            results = executor.map(
                lambda f: self._process_single_image(f, resolution), files
            )
            for img in results:
                if img is not None:
                    images.append(img)
        return images
