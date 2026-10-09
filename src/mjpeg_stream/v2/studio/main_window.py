"""
studio/main_window.py
=====================
Fő ablak — összeköti a Timeline, PreviewCanvas és SettingsPanel komponenseket.
Kezeli az Export és Teszt videó folyamatokat.
"""

import os
import sys
import json
import tempfile
from typing import List, Optional

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QLabel, QPushButton, QFileDialog,
    QMessageBox, QProgressDialog, QMenuBar, QStatusBar,
    QFrame, QApplication
)
from PyQt6.QtCore import Qt, QProcess, QTimer, pyqtSlot
from PyQt6.QtGui import QAction, QFont, QColor, QPalette, QKeySequence

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from backend.image_processor import LogoItem, FrameSpec
from studio.timeline_panel import TimelinePanel
from studio.preview_canvas import PreviewCanvas
from studio.settings_panel import SettingsPanel
from studio.logo_manager import LogoManager
from studio.project_io import ProjectConfig, ProjectIO

WINDOW_TITLE = "MJPEG Studio"
BACKEND_SCRIPT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "make_mjpeg.py")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self._config = ProjectConfig()
        self._logo_manager = LogoManager()
        self._last_export_path: Optional[str] = None
        self._process: Optional[QProcess] = None
        self._progress_dlg: Optional[QProgressDialog] = None
        
        self._autosave_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "autosave.mjpeg_proj")

        self.setWindowTitle(WINDOW_TITLE)
        self.resize(1300, 820)
        self._apply_dark_theme()
        self._build_ui()
        self._connect_signals()
        
        # Próbáljuk betölteni az autosave-et
        if os.path.exists(self._autosave_path):
            try:
                self._load_project(self._autosave_path)
                self._update_status(f"Automatikus mentés betöltve.")
            except Exception:
                self._update_status("Kész. Húzz képeket a Timeline-ra a kezdéshez!")
        else:
            self._update_status("Kész. Húzz képeket a Timeline-ra a kezdéshez!")

    # ------------------------------------------------------------------
    # UI építés
    # ------------------------------------------------------------------

    def _apply_dark_theme(self):
        self.setStyleSheet("""
            QMainWindow, QWidget { background: #14161c; color: #c8ccd6; font-family: 'Segoe UI'; }
            QMenuBar { background: #1a1c22; color: #c8ccd6; padding: 2px; }
            QMenuBar::item:selected { background: #2a3a5a; border-radius: 4px; }
            QMenu { background: #1e2028; border: 1px solid #3a3d4a; color: #c8ccd6; }
            QMenu::item:selected { background: #2a3a5a; }
            QStatusBar { background: #1a1c22; color: #7a8090; font-size: 9pt; }
            QSplitter::handle { background: #2a2c34; width: 3px; }
            QPushButton#exportBtn {
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 #3a7ad0, stop:1 #2a5aa0);
                border: 1px solid #4a8ae0; border-radius: 6px;
                color: white; font-weight: bold; font-size: 11pt;
                padding: 8px 20px; min-width: 130px;
            }
            QPushButton#exportBtn:hover { background: #4a8ae0; }
            QPushButton#exportBtn:pressed { background: #1a3a70; }
            QPushButton#testBtn {
                background: #2a3a28; border: 1px solid #3a5a38; border-radius: 6px;
                color: #80d080; font-size: 10pt; padding: 7px 14px;
            }
            QPushButton#testBtn:hover { background: #3a5a38; }
            QProgressDialog { background: #1e2028; color: #c8ccd6; }
        """)

    def _build_ui(self):
        # Menüsor
        self._build_menu()

        # Központi widget
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        # Felső toolbar (Export gombok)
        toolbar = self._build_toolbar()
        main_layout.addWidget(toolbar)

        # Elválasztó vonal
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("background: #2a2c34; max-height: 1px;")
        main_layout.addWidget(sep)

        # Középső terület (Preview + Settings)
        mid_splitter = QSplitter(Qt.Orientation.Horizontal)
        mid_splitter.setHandleWidth(3)

        self._preview = PreviewCanvas()
        self._settings = SettingsPanel()

        mid_splitter.addWidget(self._preview)
        mid_splitter.addWidget(self._settings)
        mid_splitter.setStretchFactor(0, 1)
        mid_splitter.setStretchFactor(1, 0)
        mid_splitter.setSizes([960, 300])

        main_layout.addWidget(mid_splitter, stretch=1)

        # Elválasztó
        sep2 = QFrame()
        sep2.setFrameShape(QFrame.Shape.HLine)
        sep2.setStyleSheet("background: #2a2c34; max-height: 1px;")
        main_layout.addWidget(sep2)

        # Timeline (alul)
        timeline_container = QWidget()
        timeline_container.setStyleSheet("background: #1a1c22;")
        tl_layout = QVBoxLayout(timeline_container)
        tl_layout.setContentsMargins(0, 0, 0, 0)
        tl_layout.setSpacing(0)

        tl_header = QLabel("  TIMELINE")
        tl_header.setStyleSheet(
            "background: #1a1c22; color: #5a6070; font-size: 8pt; "
            "font-weight: bold; letter-spacing: 2px; padding: 3px 8px;"
        )
        tl_layout.addWidget(tl_header)

        self._timeline = TimelinePanel()
        tl_layout.addWidget(self._timeline)
        main_layout.addWidget(timeline_container)

        # Státuszsor
        self._status = QStatusBar()
        self.setStatusBar(self._status)

    def _build_toolbar(self) -> QWidget:
        bar = QWidget()
        bar.setStyleSheet("background: #1a1c22; padding: 4px 10px;")
        bar.setFixedHeight(52)
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(10, 4, 10, 4)

        # Bal: projekt gombok
        btn_new = QPushButton("🗋  Új projekt")
        btn_new.clicked.connect(self._new_project)
        btn_open = QPushButton("📂  Megnyitás")
        btn_open.clicked.connect(self._open_project)
        btn_save = QPushButton("💾  Mentés")
        btn_save.clicked.connect(self._save_project)
        for btn in (btn_new, btn_open, btn_save):
            btn.setStyleSheet("""
                QPushButton {
                    background: #222430; border: 1px solid #3a3d4a; border-radius: 5px;
                    color: #a0a8b8; padding: 5px 12px;
                }
                QPushButton:hover { background: #2a2e40; }
            """)
            layout.addWidget(btn)

        layout.addStretch()

        # Jobb: Export gombok
        self._btn_test = QPushButton("🎬  Teszt videó (MP4)")
        self._btn_test.setObjectName("testBtn")
        self._btn_test.setEnabled(False)
        self._btn_test.setToolTip("Az utoljára exportált .mjpeg fájlt MP4-re konvertálja ffmpeg-gel")
        self._btn_test.clicked.connect(self._export_test_video)

        self._btn_export = QPushButton("▶  Export MJPEG")
        self._btn_export.setObjectName("exportBtn")
        self._btn_export.clicked.connect(self._start_export)

        layout.addWidget(self._btn_test)
        layout.addSpacing(8)
        layout.addWidget(self._btn_export)
        return bar

    def _build_menu(self):
        mb = self.menuBar()

        # Fájl menü
        file_menu = mb.addMenu("Fájl")
        act_new = QAction("Új projekt", self)
        act_new.setShortcut(QKeySequence("Ctrl+N"))
        act_new.triggered.connect(self._new_project)
        act_open = QAction("Projekt megnyitása...", self)
        act_open.setShortcut(QKeySequence("Ctrl+O"))
        act_open.triggered.connect(self._open_project)
        act_save = QAction("Projekt mentése...", self)
        act_save.setShortcut(QKeySequence("Ctrl+S"))
        act_save.triggered.connect(self._save_project)
        file_menu.addAction(act_new)
        file_menu.addAction(act_open)
        file_menu.addAction(act_save)
        file_menu.addSeparator()
        act_quit = QAction("Kilépés", self)
        act_quit.setShortcut(QKeySequence("Ctrl+Q"))
        act_quit.triggered.connect(self.close)
        file_menu.addAction(act_quit)

        # Export menü
        exp_menu = mb.addMenu("Export")
        act_exp = QAction("Export MJPEG...", self)
        act_exp.setShortcut(QKeySequence("Ctrl+E"))
        act_exp.triggered.connect(self._start_export)
        act_mp4 = QAction("Teszt videó (MP4)...", self)
        act_mp4.triggered.connect(self._export_test_video)
        exp_menu.addAction(act_exp)
        exp_menu.addAction(act_mp4)

    # ------------------------------------------------------------------
    # Signal kapcsolások
    # ------------------------------------------------------------------

    def _connect_signals(self):
        # Timeline
        self._timeline.frame_selected.connect(self._on_frame_selected)
        self._timeline.order_changed.connect(self._on_order_changed)
        self._timeline.frames_added.connect(self._on_frames_added)

        # Preview
        self._preview.logos_changed.connect(self._on_logos_changed)
        self._preview.logo_selected.connect(self._on_logo_selected_on_canvas)

        # Settings
        self._settings.resolution_changed.connect(self._on_resolution_changed)
        self._settings.aspect_mode_changed.connect(self._on_aspect_changed)
        self._settings.frame_rotation_changed.connect(self._on_frame_rot_changed)
        self._settings.target_size_changed.connect(self._on_target_size_changed)
        self._settings.rst_points_changed.connect(self._on_rst_changed)
        self._settings.logo_add_requested.connect(self._on_add_logo)
        self._settings.logo_apply_all_requested.connect(self._on_apply_logos_all)
        self._settings.logo_remove_requested.connect(self._on_remove_logo)
        self._settings.logo_selection_changed.connect(self._on_logo_selection_changed)
        self._settings.logo_size_changed.connect(self._on_logo_size_changed)
        self._settings.logo_rotation_changed.connect(self._on_logo_rotation_changed)

    # ------------------------------------------------------------------
    # Timeline slot-ok
    # ------------------------------------------------------------------

    @pyqtSlot(int)
    def _on_frame_selected(self, index: int):
        path = self._timeline.get_selected_path()
        logos = self._logo_manager.get_logos(index)
        mode = "crop"
        rot = 0
        if 0 <= index < len(self._config.frames):
            mode = self._config.frames[index].aspect_mode
            rot = self._config.frames[index].frame_rotation
            
        self._settings.set_aspect_mode(mode)
        self._settings.set_frame_rotation(rot)
        self._preview.set_frame(path, mode, rot, self._settings.get_resolution())
        self._preview.set_logos(logos)
        self._settings.update_logo_list(logos)

    @pyqtSlot(list)
    def _on_order_changed(self, paths: List[str]):
        self._logo_manager.set_frame_count(len(paths))
        self._update_config_frames(paths)
        self._trigger_autosave()

    @pyqtSlot(list)
    def _on_frames_added(self, new_paths: List[str]):
        total = sum(
            os.path.getsize(p) for p in self._timeline.get_frame_paths()
            if os.path.exists(p)
        )
        self._settings.update_source_total_size(total)
        self._update_status(f"{len(self._timeline.get_frame_paths())} képkocka a Timeline-on")

    # ------------------------------------------------------------------
    # Preview (logó) slot-ok
    # ------------------------------------------------------------------

    @pyqtSlot(list)
    def _on_logos_changed(self, logos: List[LogoItem]):
        idx = self._timeline.get_selected_index()
        if idx >= 0:
            self._logo_manager.set_logos(idx, logos)
            self._settings.update_logo_list(logos, self._preview._selected_logo_idx)
            self._trigger_autosave()
            
    @pyqtSlot(int)
    def _on_logo_selected_on_canvas(self, logo_idx: int):
        idx = self._timeline.get_selected_index()
        if idx >= 0:
            logos = self._logo_manager.get_logos(idx)
            self._settings.update_logo_list(logos, logo_idx)

    # ------------------------------------------------------------------
    # Settings slot-ok
    # ------------------------------------------------------------------

    @pyqtSlot(tuple)
    def _on_resolution_changed(self, res):
        self._config.resolution = tuple(res)
        self._refresh_preview()
        self._trigger_autosave()

    @pyqtSlot(str)
    def _on_aspect_changed(self, mode: str):
        idx = self._timeline.get_selected_index()
        if 0 <= idx < len(self._config.frames):
            self._config.frames[idx].aspect_mode = mode
            self._refresh_preview()
            self._trigger_autosave()
            
    @pyqtSlot(int)
    def _on_frame_rot_changed(self, deg: int):
        idx = self._timeline.get_selected_index()
        if 0 <= idx < len(self._config.frames):
            self._config.frames[idx].frame_rotation = deg
            self._refresh_preview()
            self._trigger_autosave()

    @pyqtSlot(int)
    def _on_target_size_changed(self, size: int):
        self._config.target_size_bytes = size
        self._trigger_autosave()

    @pyqtSlot(int)
    def _on_rst_changed(self, points: int):
        self._config.rst_points = points
        self._trigger_autosave()

    @pyqtSlot()
    def _on_add_logo(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Logó fájl kiválasztása", "",
            "PNG képek (*.png);;Minden fájl (*.*)"
        )
        if not path:
            return
        # Alapértelmezett: jobb alsó sarok, 15% széles
        logo = LogoItem(file_path=path, x=0.80, y=0.85, width=0.15, height=0.10)
        idx = self._timeline.get_selected_index()
        if idx >= 0:
            self._logo_manager.add_logo(idx, logo)
            logos = self._logo_manager.get_logos(idx)
            self._preview.set_logos(logos, len(logos) - 1)
            self._settings.update_logo_list(logos, len(logos) - 1)
            self._trigger_autosave()

    @pyqtSlot()
    def _on_apply_logos_all(self):
        idx = self._timeline.get_selected_index()
        if idx < 0:
            return
        self._logo_manager.apply_logos_to_all(idx)
        self._update_status("Logók alkalmazva az összes képkockára.")
        self._trigger_autosave()

    @pyqtSlot(int)
    def _on_remove_logo(self, logo_idx: int):
        idx = self._timeline.get_selected_index()
        if idx >= 0:
            self._logo_manager.remove_logo(idx, logo_idx)
            logos = self._logo_manager.get_logos(idx)
            self._preview.set_logos(logos)
            self._settings.update_logo_list(logos)
            self._trigger_autosave()

    @pyqtSlot(int)
    def _on_logo_selection_changed(self, list_idx: int):
        self._preview._selected_logo_idx = list_idx
        self._preview.update()
        
    @pyqtSlot(int)
    def _on_logo_size_changed(self, pct: int):
        idx = self._timeline.get_selected_index()
        logo_idx = self._preview._selected_logo_idx
        if idx >= 0 and logo_idx >= 0:
            logos = self._logo_manager.get_logos(idx)
            if logo_idx < len(logos):
                logos[logo_idx].width = pct / 100.0
                logos[logo_idx].height = pct / 100.0 # négyzetes feltételezés, de a kép aránya tartja magát
                self._preview.update()
                self._trigger_autosave()
                
    @pyqtSlot(int)
    def _on_logo_rotation_changed(self, deg: int):
        idx = self._timeline.get_selected_index()
        logo_idx = self._preview._selected_logo_idx
        if idx >= 0 and logo_idx >= 0:
            logos = self._logo_manager.get_logos(idx)
            if logo_idx < len(logos):
                logos[logo_idx].rotation = float(deg)
                self._preview.update()
                self._trigger_autosave()

    # ------------------------------------------------------------------
    # Projekt kezelés
    # ------------------------------------------------------------------

    def _new_project(self):
        self._config = ProjectConfig()
        self._logo_manager = LogoManager()
        self._timeline.set_frames([])
        self._preview.set_frame(None, "crop", 0, (1280, 720))
        self._preview.set_logos([])
        self._settings.update_logo_list([])
        self._last_export_path = None
        self._btn_test.setEnabled(False)
        self._update_status("Új projekt létrehozva.")

    def _open_project(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Projekt megnyitása", "", "MJPEG Studio Projekt (*.mjpeg_proj);;JSON (*.json)"
        )
        if not path:
            return
        try:
            self._load_project(path)
            self._update_status(f"Projekt betöltve: {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self, "Hiba", f"A projekt nem tölthető be:\n{e}")
            
    def _load_project(self, path: str):
        cfg = ProjectIO.load(path)
        self._config = cfg
        self._logo_manager = LogoManager(len(cfg.frames))

        paths = [f.source_path for f in cfg.frames]
        for i, frame in enumerate(cfg.frames):
            self._logo_manager.set_logos(i, frame.logos)

        self._timeline.set_frames(paths)
        
        # Calculate and set source size for the slider
        total = sum(
            os.path.getsize(p) for p in paths
            if os.path.exists(p)
        )
        self._settings.update_source_total_size(total)
        
        # Restore global settings
        self._settings.set_resolution(cfg.resolution)
        self._settings.set_target_size_bytes(cfg.target_size_bytes)
        self._settings.set_rst_points(cfg.rst_points)

        if len(paths) > 0:
            # force selecting 0th frame so aspect mode is updated
            self._on_frame_selected(0)
        else:
            self._refresh_preview()

    def _save_project(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Projekt mentése", "projekt.mjpeg_proj",
            "MJPEG Studio Projekt (*.mjpeg_proj);;JSON (*.json)"
        )
        if not path:
            return
        self._sync_config()
        try:
            ProjectIO.save(self._config, path)
            self._update_status(f"Projekt mentve: {os.path.basename(path)}")
        except Exception as e:
            QMessageBox.critical(self, "Hiba", f"Mentési hiba:\n{e}")

    # ------------------------------------------------------------------
    # Export
    # ------------------------------------------------------------------

    def _start_export(self):
        frames = self._timeline.get_frame_paths()
        if not frames:
            QMessageBox.warning(self, "Figyelmeztetés", "Nincs kép a Timeline-on!")
            return

        out_path, _ = QFileDialog.getSaveFileName(
            self, "MJPEG mentési helye",
            self._config.output_file,
            "MJPEG fájl (*.mjpeg);;Minden fájl (*.*)"
        )
        if not out_path:
            return

        self._config.output_file = out_path
        self._sync_config()

        # Ideiglenes JSON projekt fájl
        tmp_json = out_path + "_studio_tmp.json"
        try:
            ProjectIO.export_for_backend(self._config, tmp_json)
        except Exception as e:
            QMessageBox.critical(self, "Hiba", f"JSON generálási hiba:\n{e}")
            return

        # Progress dialógus
        self._progress_dlg = QProgressDialog("Export folyamatban...", "Mégsem", 0, 100, self)
        self._progress_dlg.setWindowTitle("MJPEG Export")
        self._progress_dlg.setWindowModality(Qt.WindowModality.WindowModal)
        self._progress_dlg.setMinimumDuration(0)
        self._progress_dlg.setValue(0)
        self._progress_dlg.canceled.connect(self._cancel_export)
        self._progress_dlg.show()

        # QProcess indítása
        self._process = QProcess(self)
        self._process.readyReadStandardOutput.connect(self._on_process_stdout)
        self._process.finished.connect(lambda code, _: self._on_export_finished(code, out_path, tmp_json))
        self._process.start(sys.executable, [BACKEND_SCRIPT, "--json", tmp_json])

        self._update_status("Export folyamatban...")

    def _on_process_stdout(self):
        if not self._process:
            return
        raw = self._process.readAllStandardOutput().data().decode("utf-8", errors="replace")
        for line in raw.splitlines():
            line = line.strip()
            if line.startswith("PROGRESS:PACK:"):
                parts = line.split(":")
                done, total = int(parts[2]), int(parts[3])
                pct = int(done / total * 100) if total else 0
                if self._progress_dlg:
                    self._progress_dlg.setValue(pct)
                    self._progress_dlg.setLabelText(f"Csomagolás: {done}/{total} képkocka...")
            elif line.startswith("PROGRESS:OPT:"):
                parts = line.split(":")
                step, q = parts[2], parts[3]
                if self._progress_dlg:
                    self._progress_dlg.setLabelText(f"Minőség optimalizálás: lépés {step}, Q={q}...")

    def _on_export_finished(self, exit_code: int, out_path: str, tmp_json: str):
        if self._progress_dlg:
            self._progress_dlg.close()
            self._progress_dlg = None

        # Takarítás
        try:
            if os.path.exists(tmp_json):
                os.remove(tmp_json)
        except Exception:
            pass

        if exit_code == 0 and os.path.exists(out_path):
            size_mb = os.path.getsize(out_path) / (1024 * 1024)
            self._last_export_path = out_path
            self._btn_test.setEnabled(True)
            QMessageBox.information(
                self, "Export kész!",
                f"Sikeresen exportálva:\n{out_path}\n\nMéret: {size_mb:.2f} MB"
            )
            self._update_status(f"Export kész: {os.path.basename(out_path)} ({size_mb:.2f} MB)")
        else:
            stderr = b""
            if self._process:
                stderr = self._process.readAllStandardError().data()
            QMessageBox.critical(
                self, "Export hiba",
                f"Az export sikertelen (kód: {exit_code}).\n\n{stderr.decode('utf-8', errors='replace')}"
            )
            self._update_status("Export sikertelen!")

        self._process = None

    def _cancel_export(self):
        if self._process and self._process.state() == QProcess.ProcessState.Running:
            self._process.kill()
        self._update_status("Export megszakítva.")

    # ------------------------------------------------------------------
    # Teszt videó (MP4)
    # ------------------------------------------------------------------

    def _export_test_video(self):
        if not self._last_export_path or not os.path.exists(self._last_export_path):
            QMessageBox.warning(self, "Figyelmeztetés", "Először exportáld az MJPEG fájlt!")
            return

        mjpeg_path = self._last_export_path
        mp4_path = os.path.splitext(mjpeg_path)[0] + ".mp4"

        reply = QMessageBox.question(
            self, "Teszt videó generálása",
            f"Az alábbi fájlt konvertálja MP4-re (1 FPS):\n{mjpeg_path}\n\nKimenet: {mp4_path}\n\nFolytatod?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        # Progress
        prog = QProgressDialog("FFmpeg konverzió folyamatban...", None, 0, 0, self)
        prog.setWindowTitle("Teszt videó")
        prog.setWindowModality(Qt.WindowModality.WindowModal)
        prog.show()

        proc = QProcess(self)
        proc.finished.connect(lambda code, _: self._on_mp4_finished(code, mp4_path, prog))
        proc.start("ffmpeg", [
            "-y", "-framerate", "1",
            "-i", mjpeg_path,
            "-filter:v", "fps=30",
            "-c:v", "libx264", "-pix_fmt", "yuv420p",
            mp4_path
        ])

        if not proc.waitForStarted(3000):
            prog.close()
            # Fallback: BAT fájl
            bat_path = os.path.join(os.path.dirname(BACKEND_SCRIPT), "mjpeg_to_mp4.bat")
            if os.path.exists(bat_path):
                QMessageBox.information(
                    self, "FFmpeg nem található",
                    f"Az ffmpeg nem elérhető a PATH-ban.\n\n"
                    f"Futtasd manuálisan:\n{bat_path}"
                )
            else:
                QMessageBox.critical(self, "Hiba", "Az ffmpeg nem érhető el!")

    def _on_mp4_finished(self, exit_code: int, mp4_path: str, prog):
        prog.close()
        if exit_code == 0 and os.path.exists(mp4_path):
            size_mb = os.path.getsize(mp4_path) / (1024 * 1024)
            QMessageBox.information(
                self, "Teszt videó kész!",
                f"MP4 sikeresen létrehozva:\n{mp4_path}\nMéret: {size_mb:.2f} MB"
            )
            self._update_status(f"Teszt videó kész: {os.path.basename(mp4_path)}")
        else:
            QMessageBox.critical(self, "Hiba", f"FFmpeg sikertelen (kód: {exit_code}).")

    # ------------------------------------------------------------------
    # Segédmetódusok
    # ------------------------------------------------------------------

    def _trigger_autosave(self):
        """Késleltetve (vagy azonnal) elmenti az állapotot."""
        self._sync_config()
        try:
            ProjectIO.save(self._config, self._autosave_path)
        except Exception:
            pass

    def _refresh_preview(self):
        idx = self._timeline.get_selected_index()
        path = self._timeline.get_selected_path()
        logos = self._logo_manager.get_logos(idx) if idx >= 0 else []
        mode = "crop"
        rot = 0
        if 0 <= idx < len(self._config.frames):
            mode = self._config.frames[idx].aspect_mode
            rot = self._config.frames[idx].frame_rotation
        self._preview.set_frame(path, mode, rot, self._settings.get_resolution())
        self._preview.set_logos(logos, self._preview._selected_logo_idx)

    def _update_config_frames(self, paths: List[str]):
        # Meglévő framékhez meg kell tartani az aspect_mode-ot, ha lehetséges
        old_frames = {f.source_path: f for f in self._config.frames}
        
        frames = []
        for i, p in enumerate(paths):
            logos = self._logo_manager.get_logos(i)
            old_f = old_frames.get(p)
            mode = old_f.aspect_mode if old_f else "crop"
            rot = old_f.frame_rotation if old_f else 0
            frames.append(FrameSpec(source_path=p, aspect_mode=mode, frame_rotation=rot, logos=logos))
        self._config.frames = frames

    def _sync_config(self):
        """Szinkronizálja a konfigurációt a GUI jelenlegi állapotával."""
        self._config.resolution = self._settings.get_resolution()
        self._config.target_size_bytes = self._settings.get_target_size_bytes()
        self._config.rst_points = self._settings.get_rst_points()
        self._update_config_frames(self._timeline.get_frame_paths())

    def _update_status(self, msg: str):
        self._status.showMessage(f"  {msg}")
