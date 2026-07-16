"""
studio/timeline_panel.py
========================
Vízszintes, görgethető bélyegkép-csík.
Támogatja a külső fájlok drag & drop behúzását (Windows Intézőből)
és a belső átrendezést (egérrel fogd és húzd).
"""

import os
from typing import List, Optional

from PyQt6.QtWidgets import (
    QWidget, QScrollArea, QHBoxLayout, QVBoxLayout,
    QLabel, QFrame, QSizePolicy, QApplication
)
from PyQt6.QtCore import (
    Qt, pyqtSignal, QSize, QPoint, QMimeData,
    QTimer, QRect
)
from PyQt6.QtGui import (
    QPixmap, QImage, QPainter, QColor, QPen, QBrush,
    QDragEnterEvent, QDropEvent, QDragMoveEvent,
    QMouseEvent, QPaintEvent, QFont, QDrag
)

SUPPORTED_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
THUMB_W = 110
THUMB_H = 80
THUMB_MARGIN = 6
SELECTED_BORDER = 3


def is_image_file(path: str) -> bool:
    return os.path.splitext(path)[1].lower() in SUPPORTED_EXTS


class ThumbnailItem(QFrame):
    """Egyetlen képkocka bélyegkép widget-je."""

    clicked = pyqtSignal(int)  # frame_index
    drag_started = pyqtSignal(int)  # frame_index

    def __init__(self, frame_index: int, source_path: str, parent=None):
        super().__init__(parent)
        self.frame_index = frame_index
        self.source_path = source_path
        self._selected = False
        self._drag_start_pos: Optional[QPoint] = None
        self._pixmap: Optional[QPixmap] = None

        self.setFixedSize(THUMB_W + THUMB_MARGIN * 2, THUMB_H + 30)
        self.setFrameShape(QFrame.Shape.NoFrame)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self._load_thumbnail()

    def _load_thumbnail(self):
        """Bélyegkép betöltése háttérszálon (de kis méret esetén gyors)."""
        try:
            pixmap = QPixmap(self.source_path)
            if pixmap.isNull():
                self._pixmap = None
            else:
                self._pixmap = pixmap.scaled(
                    THUMB_W, THUMB_H,
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                )
        except Exception:
            self._pixmap = None
        self.update()

    def set_selected(self, selected: bool):
        self._selected = selected
        self.update()

    def paintEvent(self, event: QPaintEvent):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Háttér
        bg_color = QColor(50, 52, 58) if self._selected else QColor(35, 37, 43)
        painter.fillRect(self.rect(), bg_color)

        # Bélyegkép
        if self._pixmap:
            px = THUMB_MARGIN + (THUMB_W - self._pixmap.width()) // 2
            py = 4 + (THUMB_H - self._pixmap.height()) // 2
            painter.drawPixmap(px, py, self._pixmap)
        else:
            # Placeholder
            painter.fillRect(THUMB_MARGIN, 4, THUMB_W, THUMB_H, QColor(60, 60, 70))
            painter.setPen(QColor(150, 150, 160))
            painter.setFont(QFont("Segoe UI", 8))
            painter.drawText(
                QRect(THUMB_MARGIN, 4, THUMB_W, THUMB_H),
                Qt.AlignmentFlag.AlignCenter,
                "?"
            )

        # Sorszám felirat
        painter.setPen(QColor(200, 200, 210))
        painter.setFont(QFont("Segoe UI", 7))
        painter.drawText(
            QRect(0, THUMB_H + 8, self.width(), 18),
            Qt.AlignmentFlag.AlignCenter,
            f"{self.frame_index + 1:03d}"
        )

        # Kiválasztott keret
        if self._selected:
            pen = QPen(QColor(80, 160, 255), SELECTED_BORDER)
            painter.setPen(pen)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            r = self.rect().adjusted(1, 1, -1, -1)
            painter.drawRoundedRect(r, 4, 4)

    def mousePressEvent(self, event: QMouseEvent):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_start_pos = event.pos()
            self.clicked.emit(self.frame_index)

    def mouseMoveEvent(self, event: QMouseEvent):
        if (
            self._drag_start_pos is not None
            and event.buttons() & Qt.MouseButton.LeftButton
        ):
            dist = (event.pos() - self._drag_start_pos).manhattanLength()
            if dist > QApplication.startDragDistance():
                self._start_internal_drag()

    def _start_internal_drag(self):
        """Belső átrendező drag indítása."""
        drag = QDrag(self)
        mime = QMimeData()
        mime.setText(f"INTERNAL:{self.frame_index}")
        drag.setMimeData(mime)

        if self._pixmap:
            drag.setPixmap(self._pixmap.scaled(80, 60, Qt.AspectRatioMode.KeepAspectRatio))
        drag.exec(Qt.DropAction.MoveAction)


class DropZoneWidget(QWidget):
    """A timeline húzási célterülete (üres állapotban)."""
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.fillRect(self.rect(), QColor(28, 30, 36))
        pen = QPen(QColor(70, 80, 100), 2, Qt.PenStyle.DashLine)
        painter.setPen(pen)
        painter.drawRoundedRect(self.rect().adjusted(8, 8, -8, -8), 8, 8)
        painter.setPen(QColor(100, 110, 130))
        painter.setFont(QFont("Segoe UI", 11))
        painter.drawText(
            self.rect(),
            Qt.AlignmentFlag.AlignCenter,
            "⬇  Húzd ide a képfájlokat a Windows Intézőből"
        )


class TimelinePanel(QWidget):
    """
    Vízszintes, görgethető képkocka-csík.

    Signalok:
      frame_selected(int)        — Kattintásra kiválasztott képkocka indexe
      order_changed(list[str])   — Sorrend változásakor az összes elérési út listája
      frames_added(list[str])    — Új képfájlok behúzva (elérési utak)
    """

    frame_selected = pyqtSignal(int)
    order_changed = pyqtSignal(list)
    frames_added = pyqtSignal(list)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._frame_paths: List[str] = []
        self._thumbnails: List[ThumbnailItem] = []
        self._selected_index: int = -1

        self.setAcceptDrops(True)
        self.setMinimumHeight(THUMB_H + 60)
        self.setMaximumHeight(THUMB_H + 70)

        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Görgetős terület
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOn)
        self._scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._scroll.setStyleSheet("""
            QScrollArea { background: #1c1e24; border: none; }
            QScrollBar:horizontal {
                height: 10px; background: #1c1e24;
            }
            QScrollBar::handle:horizontal {
                background: #3a3d4a; border-radius: 5px; min-width: 30px;
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                width: 0;
            }
        """)

        # Tartalom widget
        self._content = QWidget()
        self._content.setStyleSheet("background: #1c1e24;")
        self._content.setAcceptDrops(True)
        self._content.dragEnterEvent = self._content_drag_enter
        self._content.dragMoveEvent = self._content_drag_move
        self._content.dropEvent = self._content_drop

        self._thumb_layout = QHBoxLayout(self._content)
        self._thumb_layout.setContentsMargins(8, 4, 8, 4)
        self._thumb_layout.setSpacing(4)
        self._thumb_layout.addStretch()

        self._scroll.setWidget(self._content)

        # Drop zone (üres állapotban)
        self._drop_zone = DropZoneWidget()
        self._drop_zone.setMinimumHeight(THUMB_H + 40)

        layout.addWidget(self._scroll)
        self._refresh_view()

    # ------------------------------------------------------------------
    # Külső API
    # ------------------------------------------------------------------

    def get_frame_paths(self) -> List[str]:
        return list(self._frame_paths)

    def set_frames(self, paths: List[str]):
        """Programmatikusan beállítja a képkockákat (pl. projekt betöltésekor)."""
        self._frame_paths = list(paths)
        self._selected_index = 0 if paths else -1
        self._rebuild_thumbnails()
        self.order_changed.emit(list(self._frame_paths))

    def get_selected_index(self) -> int:
        return self._selected_index

    def get_selected_path(self) -> Optional[str]:
        if 0 <= self._selected_index < len(self._frame_paths):
            return self._frame_paths[self._selected_index]
        return None

    def remove_selected(self):
        """Töröl kiválasztott képkockát."""
        idx = self._selected_index
        if 0 <= idx < len(self._frame_paths):
            self._frame_paths.pop(idx)
            self._selected_index = max(0, idx - 1) if self._frame_paths else -1
            self._rebuild_thumbnails()
            self.order_changed.emit(list(self._frame_paths))

    # ------------------------------------------------------------------
    # Drag & Drop (külső fájlok, Windows Intézőből)
    # ------------------------------------------------------------------

    def dragEnterEvent(self, event: QDragEnterEvent):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dragMoveEvent(self, event: QDragMoveEvent):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent):
        self._handle_file_drop(event)

    def _content_drag_enter(self, event: QDragEnterEvent):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        elif event.mimeData().hasText() and event.mimeData().text().startswith("INTERNAL:"):
            event.acceptProposedAction()
        else:
            event.ignore()

    def _content_drag_move(self, event: QDragMoveEvent):
        if event.mimeData().hasUrls() or (
            event.mimeData().hasText()
            and event.mimeData().text().startswith("INTERNAL:")
        ):
            event.acceptProposedAction()

    def _content_drop(self, event: QDropEvent):
        if event.mimeData().hasUrls():
            self._handle_file_drop(event)
        elif event.mimeData().hasText():
            text = event.mimeData().text()
            if text.startswith("INTERNAL:"):
                from_idx = int(text.split(":")[1])
                to_idx = self._find_drop_position(event.position().toPoint())
                if from_idx != to_idx:
                    self._reorder(from_idx, to_idx)

    def _handle_file_drop(self, event):
        new_paths = []
        for url in event.mimeData().urls():
            path = url.toLocalFile()
            if os.path.isfile(path) and is_image_file(path):
                new_paths.append(path)
            elif os.path.isdir(path):
                # Ha mappát húztak be, az összes képet hozzáadjuk
                for fname in sorted(os.listdir(path)):
                    fpath = os.path.join(path, fname)
                    if os.path.isfile(fpath) and is_image_file(fpath):
                        new_paths.append(fpath)

        if new_paths:
            self._frame_paths.extend(new_paths)
            self._rebuild_thumbnails()
            # Utolsó hozzáadott képet választja ki
            self._select(len(self._frame_paths) - 1)
            self.order_changed.emit(list(self._frame_paths))
            self.frames_added.emit(new_paths)
            event.acceptProposedAction()

    def _find_drop_position(self, pos: QPoint) -> int:
        """Meghatározza, hogy a drop helye melyik index után van."""
        for i, thumb in enumerate(self._thumbnails):
            center_x = thumb.pos().x() + thumb.width() // 2
            if pos.x() < center_x:
                return i
        return len(self._thumbnails)

    def _reorder(self, from_idx: int, to_idx: int):
        """Képkocka sorrendcsere."""
        path = self._frame_paths.pop(from_idx)
        insert_at = to_idx if to_idx <= from_idx else to_idx - 1
        self._frame_paths.insert(insert_at, path)
        self._selected_index = insert_at
        self._rebuild_thumbnails()
        self.order_changed.emit(list(self._frame_paths))

    # ------------------------------------------------------------------
    # Kiválasztás
    # ------------------------------------------------------------------

    def _select(self, index: int):
        if index == self._selected_index:
            return
        # Előző kijelölés törlése
        if 0 <= self._selected_index < len(self._thumbnails):
            self._thumbnails[self._selected_index].set_selected(False)
        self._selected_index = index
        if 0 <= index < len(self._thumbnails):
            self._thumbnails[index].set_selected(True)
            # Scroll that item into view
            self._scroll.ensureWidgetVisible(self._thumbnails[index])
        self.frame_selected.emit(index)

    def _on_thumb_clicked(self, frame_index: int):
        self._select(frame_index)

    # ------------------------------------------------------------------
    # Rebuild
    # ------------------------------------------------------------------

    def _rebuild_thumbnails(self):
        """Újraépíti az összes bélyegkép widget-et."""
        # Régi widgetek törlése
        for thumb in self._thumbnails:
            self._thumb_layout.removeWidget(thumb)
            thumb.deleteLater()
        self._thumbnails.clear()

        if not self._frame_paths:
            self._refresh_view()
            return

        # Stretch eltávolítása
        for i in reversed(range(self._thumb_layout.count())):
            item = self._thumb_layout.itemAt(i)
            if item and item.spacerItem():
                self._thumb_layout.removeItem(item)

        for idx, path in enumerate(self._frame_paths):
            thumb = ThumbnailItem(idx, path, self._content)
            thumb.clicked.connect(self._on_thumb_clicked)
            thumb.set_selected(idx == self._selected_index)
            self._thumb_layout.addWidget(thumb)
            self._thumbnails.append(thumb)

        self._thumb_layout.addStretch()
        self._refresh_view()

    def _refresh_view(self):
        """Drop zone megjelenítése/elrejtése."""
        has_frames = bool(self._frame_paths)
        self._scroll.setVisible(has_frames)
        # Drop zone overlay ha nincs kép
        if not has_frames:
            if self._drop_zone.parent() is None:
                layout = self.layout()
                layout.addWidget(self._drop_zone)
            self._drop_zone.setVisible(True)
        else:
            self._drop_zone.setVisible(False)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Delete:
            self.remove_selected()
        elif event.key() == Qt.Key.Key_Left:
            self._select(max(0, self._selected_index - 1))
        elif event.key() == Qt.Key.Key_Right:
            self._select(min(len(self._frame_paths) - 1, self._selected_index + 1))
        else:
            super().keyPressEvent(event)
