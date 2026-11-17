"""
studio/logo_manager.py
======================
Logó adatmodell és kezelő logika a GUI számára.
"""

import os
from copy import deepcopy
from typing import List, Optional, Dict

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from backend.image_processor import LogoItem


class LogoManager:
    """
    Kezeli az összes képkocka logóit.
    
    Adatstruktúra:
      _logos[frame_index] = List[LogoItem]
    """

    def __init__(self, frame_count: int = 0):
        self._logos: Dict[int, List[LogoItem]] = {}
        self._frame_count = frame_count

    def set_frame_count(self, count: int):
        """Beállítja a képkockák számát. A törölt index-ek logói elvesznek."""
        self._frame_count = count
        # Töröljük az out-of-bounds logókat
        for idx in list(self._logos.keys()):
            if idx >= count:
                del self._logos[idx]

    def insert_frame(self, position: int):
        """Új képkocka beillesztése a megadott pozícióba — a logókat eltolva."""
        new_logos = {}
        for idx, logos in self._logos.items():
            if idx >= position:
                new_logos[idx + 1] = logos
            else:
                new_logos[idx] = logos
        new_logos[position] = []
        self._logos = new_logos
        self._frame_count += 1

    def remove_frame(self, position: int):
        """Képkocka eltávolítása — a logók indexei eltolva."""
        new_logos = {}
        for idx, logos in self._logos.items():
            if idx < position:
                new_logos[idx] = logos
            elif idx > position:
                new_logos[idx - 1] = logos
            # idx == position → töröljük
        self._logos = new_logos
        self._frame_count = max(0, self._frame_count - 1)

    def move_frame(self, from_idx: int, to_idx: int):
        """Képkocka áthelyezése (drag & drop sorrend csere)."""
        logos_at_from = self._logos.pop(from_idx, [])
        logos_at_to = self._logos.pop(to_idx, [])
        self._logos[from_idx] = logos_at_to
        self._logos[to_idx] = logos_at_from

    def get_logos(self, frame_index: int) -> List[LogoItem]:
        """Visszaadja a megadott képkocka logóit."""
        return self._logos.get(frame_index, [])

    def set_logos(self, frame_index: int, logos: List[LogoItem]):
        """Beállítja a megadott képkocka logóit."""
        self._logos[frame_index] = logos

    def add_logo(self, frame_index: int, logo: LogoItem):
        """Logó hozzáadása egy képkockához."""
        if frame_index not in self._logos:
            self._logos[frame_index] = []
        # Ne duplikáljuk ugyanazt a fájlt
        existing_paths = {l.file_path for l in self._logos[frame_index]}
        if logo.file_path not in existing_paths:
            self._logos[frame_index].append(logo)

    def remove_logo(self, frame_index: int, logo_index: int):
        """Logó eltávolítása egy képkockáról."""
        logos = self._logos.get(frame_index, [])
        if 0 <= logo_index < len(logos):
            logos.pop(logo_index)

    def apply_logos_to_all(self, source_frame_index: int):
        """
        A megadott képkocka logóit HOZZÁADJA az összes többi képkockához.
        (A opció: additive merge, nem felülírás)
        
        Ha egy logó fájlneve már létezik a célon, nem duplikáljuk.
        """
        source_logos = self.get_logos(source_frame_index)
        if not source_logos:
            return

        for frame_idx in range(self._frame_count):
            existing_paths = {l.file_path for l in self.get_logos(frame_idx)}
            for logo in source_logos:
                if logo.file_path not in existing_paths:
                    # Mély másolat, hogy a pozíció/méret független legyen
                    new_logo = deepcopy(logo)
                    if frame_idx not in self._logos:
                        self._logos[frame_idx] = []
                    self._logos[frame_idx].append(new_logo)
                    existing_paths.add(logo.file_path)

    def get_all_as_frame_specs_logos(self) -> Dict[int, List[LogoItem]]:
        """Az összes logó adatának visszaadása index → lista formában."""
        return {idx: list(logos) for idx, logos in self._logos.items()}
