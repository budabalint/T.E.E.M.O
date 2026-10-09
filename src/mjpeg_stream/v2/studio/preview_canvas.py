"""
studio/preview_canvas.py
========================
Élő előnézeti vászon: megjelenít egy képkockát a kiválasztott illesztési móddal,
és interaktívan elhelyezhető / méretezhető logókat rajzol rá.
"""

import os
from typing import List, Optional, Tuple

from PyQt6.QtWidgets import QWidget, QSizePolicy
from PyQt6.QtCore import Qt, pyqtSignal, QRect, QPoint, QSize, QRectF
from PyQt6.QtGui import (
    QPainter, QPixmap, QImage, QColor, QPen, QBrush, QCursor,
    QPaintEvent, QMouseEvent, QResizeEvent, QFont
)

try:
    from PIL import Image, ImageFilter, ImageQt
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from backend.image_processor import LogoItem

# Handle size in pixels (the square corners for resizing)
HANDLE_SIZE = 10
MIN_LOGO_SIZE = 0.02   # minimum 2% of frame dimension


class LogoOverlay:
    """Egy logó overlay-t reprezentál a preview-on."""

    def __init__(self, logo_item: LogoItem, logo_idx: int):
        self.logo_item = logo_item  # normalizált koordinátákkal
        self.logo_idx = logo_idx
        self._pixmap: Optional[QPixmap] = None
        self._load_pixmap()

    def _load_pixmap(self):
        try:
            self._pixmap = QPixmap(self.logo_item.file_path)
        except Exception:
            self._pixmap = None

    def get_rect_px(self, canvas_w: int, canvas_h: int) -> QRect:
        """Normalizált koordinátákból pixelkoordinátát számít (a preview méretéhez)."""
        x = int(self.logo_item.x * canvas_w)
        y = int(self.logo_item.y * canvas_h)
        w = max(10, int(self.logo_item.width * canvas_w))
        if self._pixmap and not self._pixmap.isNull():
            aspect = self._pixmap.width() / self._pixmap.height() if self._pixmap.height() != 0 else 1.0
            h = max(10, int(w / aspect))
        else:
            h = max(10, int(self.logo_item.height * canvas_h))
            
        return QRect(x, y, w, h)

    def update_from_rect_px(self, rect: QRect, canvas_w: int, canvas_h: int):
        """Pixelkoordinátából frissíti a normalizált értékeket."""
        self.logo_item.x = max(0.0, rect.x() / canvas_w)
        self.logo_item.y = max(0.0, rect.y() / canvas_h)
        self.logo_item.width = max(MIN_LOGO_SIZE, rect.width() / canvas_w)
        self.logo_item.height = max(MIN_LOGO_SIZE, rect.height() / canvas_h)

    def draw(self, painter: QPainter, canvas_w: int, canvas_h: int, selected: bool):
        """Logoqt rajzolása."""
        rect = self.get_rect_px(canvas_w, canvas_h)
        
        painter.save()
        # Elforgatáshoz a középpontba toljuk
        center = rect.center()
        painter.translate(center)
        painter.rotate(self.logo_item.rotation)
        painter.translate(-center)
        
        if self._pixmap and not self._pixmap.isNull():
            painter.drawPixmap(rect, self._pixmap.scaled(
                rect.size(),
                Qt.AspectRatioMode.IgnoreAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            ))
        else:
            painter.fillRect(rect, QColor(255, 100, 100, 100))

        if selected:
            # Kiválasztott keret rajzolása
            pen = QPen(QColor(80, 180, 255), 2, Qt.PenStyle.SolidLine)
            painter.setPen(pen)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRect(rect)
        painter.restore()

    def get_handle_at(self, pos: QPoint, canvas_w: int, canvas_h: int) -> int:
        """Kivettük a resize handle-eket, mert csúszkával állítjuk a méretet."""
        return 0

    def contains(self, pos: QPoint, canvas_w: int, canvas_h: int) -> bool:
        return self.get_rect_px(canvas_w, canvas_h).contains(pos)


class PreviewCanvas(QWidget):
    """
    Élő előnézeti widget.
    - Megjelenít egy képkockát (crop/letterbox/blur módban)
    - Logókat rajzol rá, amelyek egérrel mozgathatók és méretezhetők
    - A logó-módosítások normalizált koordinátákban tárolódnak

    Signalok:
      logos_changed(list[LogoItem])  — Ha a felhasználó módosít egy logót
    """

    logos_changed = pyqtSignal(list)
    logo_selected = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._source_path: Optional[str] = None
        self._aspect_mode: str = "crop"
        self._frame_rotation: int = 0
        self._resolution: Tuple[int, int] = (1280, 720)
        self._logo_overlays: List[LogoOverlay] = []
        self._selected_logo_idx: int = -1

        # Drag state
        self._drag_logo_idx: int = -1
        self._drag_start_mouse: Optional[QPoint] = None
        self._drag_start_rect: Optional[QRect] = None

        # Rendered kép cache
        self._bg_pixmap: Optional[QPixmap] = None
        self._last_source: Optional[str] = None
        self._last_mode: Optional[str] = None
        self._last_frame_rotation: Optional[int] = None
        self._last_canvas_size: Optional[Tuple[int, int]] = None

        self.setMinimumSize(320, 240)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setStyleSheet("background: #12141a;")
        self.setMouseTracking(True)

    # ------------------------------------------------------------------
    # Külső API
    # ------------------------------------------------------------------

    def set_frame(self, source_path: Optional[str], aspect_mode: str, frame_rotation: int, resolution: Tuple[int, int]):
        """Képkocka beállítása — újrarajzolást triggerál."""
        changed = (
            source_path != self._source_path
            or aspect_mode != self._aspect_mode
            or frame_rotation != self._frame_rotation
            or resolution != self._resolution
        )
        self._source_path = source_path
        self._aspect_mode = aspect_mode
        self._frame_rotation = frame_rotation
        self._resolution = resolution
        if changed:
            self._bg_pixmap = None  # invalidate cache
        self.update()

    def set_logos(self, logos: List[LogoItem], selected_idx: int = -1):
        """Logók beállítása (minden frame-váltásnál meghívódik)."""
        self._logo_overlays = [LogoOverlay(logo, i) for i, logo in enumerate(logos)]
        self._selected_logo_idx = selected_idx
        self.update()

    def get_logos(self) -> List[LogoItem]:
        return [ov.logo_item for ov in self._logo_overlays]

    def add_logo(self, logo: LogoItem):
        idx = len(self._logo_overlays)
        overlay = LogoOverlay(logo, idx)
        self._logo_overlays.append(overlay)
        self._selected_logo_idx = idx
        self.update()
        self.logos_changed.emit(self.get_logos())
        self.logo_selected.emit(idx)

    def remove_selected_logo(self):
        if 0 <= self._selected_logo_idx < len(self._logo_overlays):
            self._logo_overlays.pop(self._selected_logo_idx)
            # Újraszámozzuk az indexeket
            for i, ov in enumerate(self._logo_overlays):
                ov.logo_idx = i
            self._selected_logo_idx = -1
            self.update()
            self.logos_changed.emit(self.get_logos())
            self.logo_selected.emit(-1)

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------

    def _get_canvas_rect(self) -> QRect:
        """A widget-en belüli, arányosan elhelyezett kép területe."""
        w, h = self.width(), self.height()
        target_w, target_h = self._resolution
        scale = min(w / target_w, h / target_h)
        dw = int(target_w * scale)
        dh = int(target_h * scale)
        x = (w - dw) // 2
        y = (h - dh) // 2
        return QRect(x, y, dw, dh)

    def _render_background(self, canvas_w: int, canvas_h: int) -> Optional[QPixmap]:
        """A háttérkép renderelése a megadott méretben."""
        if not self._source_path or not HAS_PIL:
            return None

        cache_key = (self._source_path, self._aspect_mode, canvas_w, canvas_h)
        if (
            self._bg_pixmap is not None
            and self._last_source == self._source_path
            and self._last_mode == self._aspect_mode
            and self._last_frame_rotation == self._frame_rotation
            and self._last_canvas_size == (canvas_w, canvas_h)
        ):
            return self._bg_pixmap

        try:
            img = Image.open(self._source_path).convert("RGB")
            
            # Fő kép elforgatása
            if self._frame_rotation == 90:
                img = img.transpose(Image.Transpose.ROTATE_270)
            elif self._frame_rotation == 180:
                img = img.transpose(Image.Transpose.ROTATE_180)
            elif self._frame_rotation == 270:
                img = img.transpose(Image.Transpose.ROTATE_90)
                
        except Exception:
            return None

        # Átméretezés az illesztési mód szerint
        mode = self._aspect_mode
        if mode == "crop":
            img = self._crop(img, canvas_w, canvas_h)
        elif mode == "letterbox":
            img = self._letterbox(img, canvas_w, canvas_h)
        elif mode == "blur":
            img = self._blur_fill(img, canvas_w, canvas_h)

        # PIL → QPixmap
        qimg = QImage(
            img.tobytes("raw", "RGB"),
            img.width, img.height,
            img.width * 3,
            QImage.Format.Format_RGB888,
        )
        pixmap = QPixmap.fromImage(qimg)

        self._bg_pixmap = pixmap
        self._last_source = self._source_path
        self._last_mode = self._aspect_mode
        self._last_frame_rotation = self._frame_rotation
        self._last_canvas_size = (canvas_w, canvas_h)
        return pixmap

    @staticmethod
    def _crop(img: Image.Image, w: int, h: int) -> Image.Image:
        src_w, src_h = img.size
        ratio = max(w / src_w, h / src_h)
        nw, nh = int(src_w * ratio), int(src_h * ratio)
        img = img.resize((nw, nh), Image.Resampling.LANCZOS)
        left = (nw - w) // 2
        top = (nh - h) // 2
        return img.crop((left, top, left + w, top + h))

    @staticmethod
    def _letterbox(img: Image.Image, w: int, h: int) -> Image.Image:
        img = img.copy()
        img.thumbnail((w, h), Image.Resampling.LANCZOS)
        result = Image.new("RGB", (w, h), (0, 0, 0))
        result.paste(img, ((w - img.width) // 2, (h - img.height) // 2))
        return result

    @staticmethod
    def _blur_fill(img: Image.Image, w: int, h: int) -> Image.Image:
        bg = img.resize((w, h), Image.Resampling.LANCZOS).filter(ImageFilter.GaussianBlur(20))
        fg = img.copy()
        fg.thumbnail((w, h), Image.Resampling.LANCZOS)
        bg.paste(fg, ((w - fg.width) // 2, (h - fg.height) // 2))
        return bg

    def paintEvent(self, event: QPaintEvent):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)

        # Háttér
        painter.fillRect(self.rect(), QColor(18, 20, 26))

        canvas_rect = self._get_canvas_rect()

        if self._source_path:
            bg = self._render_background(canvas_rect.width(), canvas_rect.height())
            if bg:
                painter.drawPixmap(canvas_rect, bg)
            else:
                painter.fillRect(canvas_rect, QColor(40, 42, 50))
                painter.setPen(QColor(120, 120, 140))
                painter.setFont(QFont("Segoe UI", 12))
                painter.drawText(canvas_rect, Qt.AlignmentFlag.AlignCenter, "Kép betöltése...")
        else:
            painter.fillRect(canvas_rect, QColor(30, 32, 40))
            painter.setPen(QColor(80, 90, 110))
            painter.setFont(QFont("Segoe UI", 12))
            painter.drawText(
                canvas_rect,
                Qt.AlignmentFlag.AlignCenter,
                "Nincs kiválasztott képkocka\n\nHúzz képeket a Timeline-ra"
            )

        # Logók rajzolása (a canvas koordináta-rendszerben)
        painter.translate(canvas_rect.topLeft())
        cw, ch = canvas_rect.width(), canvas_rect.height()
        for i, overlay in enumerate(self._logo_overlays):
            overlay.draw(painter, cw, ch, selected=(i == self._selected_logo_idx))
        painter.translate(-canvas_rect.left(), -canvas_rect.top())

        # Canvas keret
        pen = QPen(QColor(50, 55, 70), 1)
        painter.setPen(pen)
        painter.drawRect(canvas_rect)

    # ------------------------------------------------------------------
    # Egér interakció (logó mozgatás / méretezés)
    # ------------------------------------------------------------------

    def _canvas_pos(self, global_pos: QPoint) -> Tuple[Optional[QPoint], int, int]:
        """Widgetr koordinátát canvas-relatív koordinátává alakít."""
        canvas_rect = self._get_canvas_rect()
        local_pos = global_pos
        rel = QPoint(local_pos.x() - canvas_rect.x(), local_pos.y() - canvas_rect.y())
        return rel, canvas_rect.width(), canvas_rect.height()

    def mousePressEvent(self, event: QMouseEvent):
        if event.button() != Qt.MouseButton.LeftButton:
            super().mousePressEvent(event)
            return

        pos, cw, ch = self._canvas_pos(event.pos())

        # Megnézzük, hogy valamelyik logóra kattintottunk-e (hátulról előre, hogy a legfelsőt fogjuk meg)
        hit_logo = -1
        for i in reversed(range(len(self._logo_overlays))):
            overlay = self._logo_overlays[i]
            if overlay.contains(pos, cw, ch):
                hit_logo = i
                break

        if self._selected_logo_idx != hit_logo:
            self._selected_logo_idx = hit_logo
            self.logo_selected.emit(hit_logo)

        self._drag_logo_idx = hit_logo
        self._drag_start_mouse = pos

        if hit_logo >= 0:
            self._drag_start_rect = self._logo_overlays[hit_logo].get_rect_px(cw, ch)

        self.update()
        self.setCursor(Qt.CursorShape.ClosedHandCursor if hit_logo >= 0 else Qt.CursorShape.ArrowCursor)

    def mouseMoveEvent(self, event: QMouseEvent):
        pos, cw, ch = self._canvas_pos(event.pos())

        if self._drag_logo_idx < 0 or self._drag_start_mouse is None:
            # Kurzor frissítése
            for overlay in reversed(self._logo_overlays):
                if overlay.contains(pos, cw, ch):
                    self.setCursor(Qt.CursorShape.OpenHandCursor)
                    return
            self.setCursor(Qt.CursorShape.ArrowCursor)
            return

        dx = pos.x() - self._drag_start_mouse.x()
        dy = pos.y() - self._drag_start_mouse.y()
        overlay = self._logo_overlays[self._drag_logo_idx]
        start_rect = self._drag_start_rect

        # Csak mozgatás
        new_rect = QRect(
            start_rect.x() + dx,
            start_rect.y() + dy,
            start_rect.width(),
            start_rect.height(),
        )

        overlay.update_from_rect_px(new_rect, cw, ch)
        self.update()

    def mouseReleaseEvent(self, event: QMouseEvent):
        if self._drag_logo_idx >= 0:
            self.logos_changed.emit(self.get_logos())
        self._drag_logo_idx = -1
        self._drag_start_mouse = None
        self._drag_start_rect = None
        self.setCursor(Qt.CursorShape.ArrowCursor)

    def resizeEvent(self, event: QResizeEvent):
        self._bg_pixmap = None  # Canvas méretváltozáskor invalidáljuk a cache-t
        self.update()
