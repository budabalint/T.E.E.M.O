"""
studio/settings_panel.py
========================
Jobb oldali beállítások panel.
Felbontás, illesztési mód, célméret csúszka, RST csúszka, logó kezelés.
"""

import os
from typing import List, Optional, Tuple

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QSlider,
    QComboBox, QPushButton, QFileDialog, QListWidget,
    QListWidgetItem, QGroupBox, QSizePolicy, QFrame,
    QLineEdit, QSpinBox, QToolButton, QScrollArea
)
from PyQt6.QtCore import Qt, pyqtSignal, QSize
from PyQt6.QtGui import QIcon, QFont, QColor

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from backend.image_processor import LogoItem

MB = 1024 * 1024

RESOLUTIONS = [
    ("320 × 240", (320, 240)),
    ("640 × 480", (640, 480)),
    ("1280 × 720  (HD)", (1280, 720)),
    ("1920 × 1080 (Full HD)", (1920, 1080)),
    ("2560 × 1440 (QHD)", (2560, 1440)),
    ("Egyéni...", None),
]

ASPECT_MODES = [
    ("Vágás (Crop)",         "crop"),
    ("Fekete sávok (Letterbox)", "letterbox"),
    ("Elhomályosított háttér (Blur)", "blur"),
]


def _section_label(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setFont(QFont("Segoe UI", 8, QFont.Weight.Bold))
    lbl.setStyleSheet("color: #7a8090; letter-spacing: 1px; margin-top: 8px;")
    return lbl


def _make_slider(min_v: int, max_v: int, val: int) -> QSlider:
    s = QSlider(Qt.Orientation.Horizontal)
    s.setRange(min_v, max_v)
    s.setValue(val)
    s.setStyleSheet("""
        QSlider::groove:horizontal {
            height: 6px; background: #2d2f38; border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #5090e0; border-radius: 7px;
            width: 14px; height: 14px; margin: -4px 0;
        }
        QSlider::sub-page:horizontal {
            background: #3a6ab5; border-radius: 3px;
        }
    """)
    return s


class SettingsPanel(QWidget):
    """
    Jobb oldali beállítások panel.

    Signalok:
      resolution_changed(tuple)      — Új (w, h) felbontás
      aspect_mode_changed(str)       — "crop" | "letterbox" | "blur"
      target_size_changed(int)       — Célméret bájtban (0 = automatikus)
      rst_points_changed(int)        — RST pontok száma (0 = kikapcsolt)
      logo_add_requested()           — Felhasználó logót akar hozzáadni
      logo_apply_all_requested()     — Logókat minden képre alkalmazni
      logo_remove_requested(int)     — Logó törlése (lista index)
    """

    resolution_changed = pyqtSignal(tuple)
    aspect_mode_changed = pyqtSignal(str)
    frame_rotation_changed = pyqtSignal(int)
    target_size_changed = pyqtSignal(int)
    rst_points_changed = pyqtSignal(int)
    logo_add_requested = pyqtSignal()
    logo_apply_all_requested = pyqtSignal()
    logo_remove_requested = pyqtSignal(int)
    logo_selection_changed = pyqtSignal(int)
    logo_size_changed = pyqtSignal(int)
    logo_rotation_changed = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._resolution: Tuple[int, int] = (1280, 720)
        self._total_source_bytes: int = 0
        self._target_size_bytes: int = 0  # 0 = auto
        self._slider_enabled: bool = True

        self.setMinimumWidth(270)
        self.setMaximumWidth(340)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)
        self.setStyleSheet("""
            QWidget { background: #1a1c22; color: #c8ccd6; font-family: 'Segoe UI'; font-size: 10pt; }
            QGroupBox {
                border: 1px solid #2d2f3a;
                border-radius: 6px;
                margin-top: 10px;
                padding: 6px;
                font-weight: bold;
                color: #9098a8;
                font-size: 9pt;
            }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QComboBox {
                background: #22242c; border: 1px solid #3a3d4a; border-radius: 4px;
                padding: 4px 8px; color: #c8ccd6;
            }
            QComboBox::drop-down { border: none; }
            QComboBox QAbstractItemView { background: #22242c; selection-background-color: #3a6ab5; }
            QPushButton {
                background: #2a3a5a; border: 1px solid #3a5080; border-radius: 5px;
                color: #a0c0f0; padding: 5px 10px;
            }
            QPushButton:hover { background: #3a5a8a; }
            QPushButton:pressed { background: #1a2a4a; }
            QListWidget {
                background: #1e2028; border: 1px solid #2d2f3a; border-radius: 4px;
                color: #c8ccd6;
            }
            QListWidget::item:selected { background: #2a3a5a; }
            QLineEdit {
                background: #22242c; border: 1px solid #3a3d4a; border-radius: 4px;
                padding: 3px 6px; color: #c8ccd6;
            }
        """)

        self._build_ui()

    # ------------------------------------------------------------------
    # UI építés
    # ------------------------------------------------------------------

    def _build_ui(self):
        main_outer = QVBoxLayout(self)
        main_outer.setContentsMargins(0, 0, 0, 0)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setStyleSheet("""
            QScrollArea { background: transparent; border: none; }
            QScrollBar:vertical {
                width: 10px; background: #1a1c22;
            }
            QScrollBar::handle:vertical {
                background: #3a3d4a; border-radius: 5px; min-height: 30px;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0;
            }
        """)
        
        content = QWidget()
        content.setStyleSheet("background: transparent;")
        main = QVBoxLayout(content)
        main.setContentsMargins(10, 10, 10, 10)
        main.setSpacing(6)

        # --- FELBONTÁS ---
        grp_res = QGroupBox("Felbontás")
        res_lay = QVBoxLayout(grp_res)
        res_lay.setSpacing(4)

        self._res_combo = QComboBox()
        for label, _ in RESOLUTIONS:
            self._res_combo.addItem(label)
        self._res_combo.setCurrentIndex(2)  # HD
        self._res_combo.currentIndexChanged.connect(self._on_res_combo_changed)
        res_lay.addWidget(self._res_combo)

        # Egyéni felbontás beviteli sor
        custom_row = QHBoxLayout()
        self._custom_w = QSpinBox()
        self._custom_w.setRange(160, 7680)
        self._custom_w.setValue(1280)
        self._custom_w.setVisible(False)
        self._custom_h = QSpinBox()
        self._custom_h.setRange(120, 4320)
        self._custom_h.setValue(720)
        self._custom_h.setVisible(False)
        self._custom_sep = QLabel("×")
        self._custom_sep.setVisible(False)
        self._custom_w.valueChanged.connect(self._on_custom_res_changed)
        self._custom_h.valueChanged.connect(self._on_custom_res_changed)
        custom_row.addWidget(self._custom_w)
        custom_row.addWidget(self._custom_sep)
        custom_row.addWidget(self._custom_h)
        res_lay.addLayout(custom_row)

        main.addWidget(grp_res)

        # --- ILLESZTÉSI MÓD ---
        grp_asp = QGroupBox("Illesztési mód")
        asp_lay = QVBoxLayout(grp_asp)
        self._aspect_combo = QComboBox()
        for label, _ in ASPECT_MODES:
            self._aspect_combo.addItem(label)
        self._aspect_combo.currentIndexChanged.connect(self._on_aspect_changed)
        asp_lay.addWidget(self._aspect_combo)
        main.addWidget(grp_asp)

        # --- KÉP ELFORGATÁSA ---
        grp_rot = QGroupBox("Kép elforgatása")
        rot_lay = QVBoxLayout(grp_rot)
        self._frame_rot_combo = QComboBox()
        self._frame_rot_combo.addItem("0° (Eredeti)", 0)
        self._frame_rot_combo.addItem("90° (Óramutatóval megegyező)", 90)
        self._frame_rot_combo.addItem("180° (Fejjel lefelé)", 180)
        self._frame_rot_combo.addItem("270° (Óramutatóval ellentétes)", 270)
        self._frame_rot_combo.currentIndexChanged.connect(self._on_frame_rot_changed)
        rot_lay.addWidget(self._frame_rot_combo)
        main.addWidget(grp_rot)

        # --- CÉLMÉRET ---
        grp_size = QGroupBox("Célméret")
        size_lay = QVBoxLayout(grp_size)
        size_lay.setSpacing(4)

        self._size_label = QLabel("Automatikus (korlát nélkül)")
        self._size_label.setStyleSheet("color: #70b870; font-size: 9pt;")
        size_lay.addWidget(self._size_label)

        self._size_slider = _make_slider(1, 100, 50)
        self._size_slider.valueChanged.connect(self._on_size_slider_changed)
        size_lay.addWidget(self._size_slider)

        self._size_range_label = QLabel("Min: 1 MB   |   Max: auto")
        self._size_range_label.setStyleSheet("color: #5a6070; font-size: 8pt;")
        size_lay.addWidget(self._size_range_label)

        main.addWidget(grp_size)

        # --- RST MARKER ---
        grp_rst = QGroupBox("Restart Marker (RST)")
        rst_lay = QVBoxLayout(grp_rst)
        rst_lay.setSpacing(4)

        self._rst_label = QLabel("Újraindítási pontok száma: 0 db (kikapcsolt)")
        self._rst_label.setStyleSheet("color: #9098a8; font-size: 9pt;")
        self._rst_label.setWordWrap(True)
        rst_lay.addWidget(self._rst_label)

        self._rst_slider = _make_slider(0, 50, 0)
        self._rst_slider.valueChanged.connect(self._on_rst_changed)
        rst_lay.addWidget(self._rst_slider)

        main.addWidget(grp_rst)

        # --- LOGÓK ---
        grp_logo = QGroupBox("Logók / Vízjelek")
        logo_lay = QVBoxLayout(grp_logo)
        logo_lay.setSpacing(4)

        btn_row = QHBoxLayout()
        self._btn_add_logo = QPushButton("+ Logó hozzáadása")
        self._btn_add_logo.clicked.connect(self.logo_add_requested.emit)
        self._btn_apply_all = QPushButton("→ Minden képre")
        self._btn_apply_all.setToolTip("Az aktuális kép logóit hozzáadja az összes képkockához")
        self._btn_apply_all.clicked.connect(self.logo_apply_all_requested.emit)
        btn_row.addWidget(self._btn_add_logo)
        btn_row.addWidget(self._btn_apply_all)
        logo_lay.addLayout(btn_row)

        self._logo_list = QListWidget()
        self._logo_list.setMaximumHeight(80)
        self._logo_list.setSelectionMode(QListWidget.SelectionMode.SingleSelection)
        self._logo_list.currentRowChanged.connect(self.logo_selection_changed.emit)
        logo_lay.addWidget(self._logo_list)

        # Logó tulajdonságok (csúszkák)
        self._logo_props = QWidget()
        props_lay = QVBoxLayout(self._logo_props)
        props_lay.setContentsMargins(0, 0, 0, 0)
        
        self._logo_size_lbl = QLabel("Méret: 15%")
        props_lay.addWidget(self._logo_size_lbl)
        self._logo_size_slider = _make_slider(1, 100, 15)
        self._logo_size_slider.valueChanged.connect(self._on_logo_size_changed)
        props_lay.addWidget(self._logo_size_slider)
        
        self._logo_rot_lbl = QLabel("Forgatás: 0°")
        props_lay.addWidget(self._logo_rot_lbl)
        self._logo_rot_slider = _make_slider(-180, 180, 0)
        self._logo_rot_slider.valueChanged.connect(self._on_logo_rot_changed)
        props_lay.addWidget(self._logo_rot_slider)
        
        self._logo_props.setEnabled(False)
        logo_lay.addWidget(self._logo_props)

        self._btn_del_logo = QPushButton("🗑  Kiválasztott törlése")
        self._btn_del_logo.setStyleSheet("color: #d06060;")
        self._btn_del_logo.clicked.connect(self._on_delete_logo)
        logo_lay.addWidget(self._btn_del_logo)

        main.addWidget(grp_logo)
        main.addStretch()
        
        scroll.setWidget(content)
        main_outer.addWidget(scroll)

    # ------------------------------------------------------------------
    def set_resolution(self, res: Tuple[int, int]):
        self._resolution = res
        self._res_combo.blockSignals(True)
        self._custom_w.blockSignals(True)
        self._custom_h.blockSignals(True)
        
        found = False
        for i, (lbl, r) in enumerate(RESOLUTIONS):
            if r == res:
                self._res_combo.setCurrentIndex(i)
                found = True
                self._custom_w.setVisible(False)
                self._custom_h.setVisible(False)
                self._custom_sep.setVisible(False)
                break
                
        if not found:
            self._res_combo.setCurrentIndex(len(RESOLUTIONS) - 1) # Egyéni
            self._custom_w.setVisible(True)
            self._custom_h.setVisible(True)
            self._custom_sep.setVisible(True)
            self._custom_w.setValue(res[0])
            self._custom_h.setValue(res[1])
            
        self._res_combo.blockSignals(False)
        self._custom_w.blockSignals(False)
        self._custom_h.blockSignals(False)

    def get_resolution(self) -> Tuple[int, int]:
        return self._resolution

    def set_aspect_mode(self, mode: str):
        """Beállítja a lenyíló értékét (pl. frame váltáskor)."""
        self._aspect_combo.blockSignals(True)
        for i, (lbl, m) in enumerate(ASPECT_MODES):
            if m == mode:
                self._aspect_combo.setCurrentIndex(i)
                break
        self._aspect_combo.blockSignals(False)

    def get_aspect_mode(self) -> str:
        idx = self._aspect_combo.currentIndex()
        return ASPECT_MODES[idx][1]
        
    def set_frame_rotation(self, deg: int):
        self._frame_rot_combo.blockSignals(True)
        for i in range(self._frame_rot_combo.count()):
            if self._frame_rot_combo.itemData(i) == deg:
                self._frame_rot_combo.setCurrentIndex(i)
                break
        self._frame_rot_combo.blockSignals(False)

    def get_target_size_bytes(self) -> int:
        """Visszaadja a célméretet bájtban. 0 = automatikus."""
        return self._target_size_bytes
        
    def set_target_size_bytes(self, size: int):
        self._target_size_bytes = size
        self._size_slider.blockSignals(True)
        if size == 0 or not self._slider_enabled:
            self._size_slider.setValue(0)
        else:
            max_bytes = self._total_source_bytes // 2
            if max_bytes <= MB:
                self._size_slider.setValue(0)
            else:
                # size = MB + t * (max_bytes - MB)
                # t = (size - MB) / (max_bytes - MB)
                # value = 1 + t * 99
                t = (size - MB) / (max_bytes - MB)
                val = 1 + t * 99
                self._size_slider.setValue(int(max(1, min(100, val))))
        
        self._update_size_label()
        self._size_slider.blockSignals(False)

    def get_rst_points(self) -> int:
        return self._rst_slider.value()
        
    def set_rst_points(self, pts: int):
        self._rst_slider.blockSignals(True)
        self._rst_slider.setValue(pts)
        self._on_rst_changed(pts) # frissíti a label-t
        self._rst_slider.blockSignals(False)

    def update_source_total_size(self, total_bytes: int):
        """
        Frissíti a célméret csúszka maximumát a behúzott képek összmérete alapján.
        Meghívódik, amikor a Timeline-ba új képek kerülnek.
        """
        self._total_source_bytes = total_bytes
        max_bytes = total_bytes // 2

        if max_bytes < MB:
            # A max kisebb mint 1 MB → csúszka letiltva
            self._size_slider.setEnabled(False)
            self._target_size_bytes = 0
            self._size_label.setText("Automatikus (képek összmérete < 2 MB)")
            self._size_label.setStyleSheet("color: #9098a8; font-size: 9pt;")
            self._slider_enabled = False
        else:
            self._size_slider.setEnabled(True)
            self._slider_enabled = True
            # 0 pozíció = automatikus, 1–100 lineárisan [1 MB … max_bytes]
            # Slider értéke 0–100: 0 → auto, 1 → 1 MB, 100 → max_bytes
            self._size_slider.setRange(0, 100)
            # Tartás ha volt korábbi beállítás
            self._update_size_label()
            max_mb = max_bytes / MB
            self._size_range_label.setText(
                f"Min: 1 MB   |   Max: {max_mb:.1f} MB (eredeti méret / 2)"
            )

    def update_logo_list(self, logos: List[LogoItem], selected_idx: int = -1):
        """Frissíti a logó listát."""
        self._logo_list.blockSignals(True)
        self._logo_list.clear()
        for logo in logos:
            name = os.path.basename(logo.file_path)
            item = QListWidgetItem(f"🖼  {name}")
            item.setToolTip(logo.file_path)
            self._logo_list.addItem(item)
        
        if 0 <= selected_idx < len(logos):
            self._logo_list.setCurrentRow(selected_idx)
            self._update_logo_props(logos[selected_idx])
            self._logo_props.setEnabled(True)
        else:
            self._logo_props.setEnabled(False)
            
        self._logo_list.blockSignals(False)

    def _update_logo_props(self, logo: LogoItem):
        self._logo_size_slider.blockSignals(True)
        self._logo_rot_slider.blockSignals(True)
        
        size_pct = int(logo.width * 100)
        self._logo_size_slider.setValue(size_pct)
        self._logo_size_lbl.setText(f"Méret: {size_pct}%")
        
        rot = int(logo.rotation)
        self._logo_rot_slider.setValue(rot)
        self._logo_rot_lbl.setText(f"Forgatás: {rot}°")
        
        self._logo_size_slider.blockSignals(False)
        self._logo_rot_slider.blockSignals(False)

    # ------------------------------------------------------------------
    # Belső slot-ok
    # ------------------------------------------------------------------

    def _on_res_combo_changed(self, idx: int):
        label, res = RESOLUTIONS[idx]
        is_custom = (res is None)
        self._custom_w.setVisible(is_custom)
        self._custom_h.setVisible(is_custom)
        self._custom_sep.setVisible(is_custom)
        if not is_custom:
            self._resolution = res
            self.resolution_changed.emit(self._resolution)

    def _on_custom_res_changed(self):
        self._resolution = (self._custom_w.value(), self._custom_h.value())
        self.resolution_changed.emit(self._resolution)

    def _on_aspect_changed(self, idx: int):
        mode = ASPECT_MODES[idx][1]
        self.aspect_mode_changed.emit(mode)
        
    def _on_frame_rot_changed(self, idx: int):
        deg = self._frame_rot_combo.itemData(idx)
        self.frame_rotation_changed.emit(deg)

    def _on_size_slider_changed(self, value: int):
        if not self._slider_enabled:
            return
        self._update_size_label()
        self.target_size_changed.emit(self._target_size_bytes)

    def _update_size_label(self):
        value = self._size_slider.value()
        max_bytes = self._total_source_bytes // 2

        if value == 0 or not self._slider_enabled:
            self._target_size_bytes = 0
            self._size_label.setText("Automatikus (korlát nélkül)")
            self._size_label.setStyleSheet("color: #70b870; font-size: 9pt;")
        else:
            # Lineáris interpoláció: 1 → 1 MB, 100 → max_bytes
            t = (value - 1) / 99.0 if value > 1 else 0.0
            size_bytes = int(MB + t * (max_bytes - MB))
            size_bytes = max(MB, min(max_bytes, size_bytes))
            self._target_size_bytes = size_bytes
            mb = size_bytes / MB
            self._size_label.setText(f"Célméret: {mb:.2f} MB")
            self._size_label.setStyleSheet("color: #80b0e0; font-size: 9pt;")

    def _on_rst_changed(self, value: int):
        if value == 0:
            self._rst_label.setText("Újraindítási pontok száma: 0 db (kikapcsolt)")
        else:
            self._rst_label.setText(f"Újraindítási pontok száma: {value} db")
        self.rst_points_changed.emit(value)
        
    def _on_logo_size_changed(self, value: int):
        self._logo_size_lbl.setText(f"Méret: {value}%")
        self.logo_size_changed.emit(value)
        
    def _on_logo_rot_changed(self, value: int):
        self._logo_rot_lbl.setText(f"Forgatás: {value}°")
        self.logo_rotation_changed.emit(value)

    def _on_delete_logo(self):
        selected = self._logo_list.selectedItems()
        if selected:
            idx = self._logo_list.row(selected[0])
            self.logo_remove_requested.emit(idx)
