"""
studio/project_io.py
====================
Projekt mentés és betöltés JSON formátumban.
A GUI összes beállítása serializálható/deserializálható.
"""

import json
import os
from typing import List, Optional, Tuple
from dataclasses import dataclass, field, asdict

# Importáljuk a backend adatstruktúrákat
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from backend.image_processor import FrameSpec, LogoItem


PROJECT_VERSION = 1


@dataclass
class ProjectConfig:
    """A teljes projektállapot — ez kerül JSON-ba."""

    version: int = PROJECT_VERSION
    resolution: Tuple[int, int] = (1280, 720)
    target_size_bytes: int = 0          # 0 = nincs méretkorlát, fix quality-t használ
    quality: int = 85                   # Fix JPEG minőség, ha target_size_bytes == 0
    rst_points: int = 0                 # 0 = RST kikapcsolt
    output_file: str = "output.mjpeg"
    frames: List[FrameSpec] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "version": self.version,
            "resolution": list(self.resolution),
            "target_size_bytes": self.target_size_bytes,
            "quality": self.quality,
            "rst_points": self.rst_points,
            "output_file": self.output_file,
            "frames": [f.to_dict() for f in self.frames],
        }

    @staticmethod
    def from_dict(d: dict) -> "ProjectConfig":
        cfg = ProjectConfig()
        cfg.version = d.get("version", PROJECT_VERSION)
        res = d.get("resolution", [1280, 720])
        cfg.resolution = (int(res[0]), int(res[1]))
        cfg.target_size_bytes = d.get("target_size_bytes", 0)
        cfg.quality = d.get("quality", 85)
        cfg.rst_points = d.get("rst_points", 0)
        cfg.output_file = d.get("output_file", "output.mjpeg")
        cfg.frames = [FrameSpec.from_dict(fd) for fd in d.get("frames", [])]
        return cfg


class ProjectIO:
    """JSON projekt fájl mentés/betöltés."""

    @staticmethod
    def save(config: ProjectConfig, path: str) -> None:
        """Projektállapot mentése JSON fájlba."""
        with open(path, "w", encoding="utf-8") as f:
            json.dump(config.to_dict(), f, indent=2, ensure_ascii=False)

    @staticmethod
    def load(path: str) -> ProjectConfig:
        """JSON fájlból projektállapot betöltése."""
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return ProjectConfig.from_dict(data)

    @staticmethod
    def export_for_backend(config: ProjectConfig, json_path: str) -> str:
        """
        Ideiglenes JSON fájl generálása a backend (make_mjpeg.py) számára.
        Visszaadja a fájl elérési útját.
        """
        ProjectIO.save(config, json_path)
        return json_path
