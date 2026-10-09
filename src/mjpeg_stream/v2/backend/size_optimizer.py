"""
backend/size_optimizer.py
=========================
Bináris kereséses méretoptimalizáló modul.

Felelőssége:
  - A lehető legjobb JPEG minőséget megtalálni, ami még belefér a célméretbe
  - GUI-barát progress callback támogatás
"""

from typing import List, Tuple, Optional, Callable

try:
    from PIL import Image
except ImportError:
    raise ImportError("A 'Pillow' könyvtár szükséges: pip install Pillow")

from .mjpeg_encoder import BitrateEstimator


class BinarySearchOptimizer:
    """
    Bináris kereséssel megkeresi a LEGNAGYOBB minőséget, ami MÉG BELEFÉR a célméretbe.

    Algoritmus:
      - Keresési tér: quality 1–100
      - Minden lépésben becsüli a teljes stream méretét
      - Maximalizálja a minőséget a méretkorlát betartása mellett
    """

    def __init__(self, estimator: Optional[BitrateEstimator] = None):
        self.estimator = estimator or BitrateEstimator()

    def find_optimal_quality(
        self,
        images: List[Image.Image],
        target_size: int,
        rst_interval: int = 0,
        progress_cb: Optional[Callable[[int, int, int], None]] = None,
    ) -> Tuple[int, int]:
        """
        Megkeresi az optimális JPEG minőséget.

        Args:
            images: PIL Image lista (már feldolgozott képek)
            target_size: Maximálisan megengedett méret bájtban
            rst_interval: RST marker MCU intervallum (0 = kikapcsolt)
            progress_cb: Opcionális callback(lépés, quality, becsült_méret_byte)
                         Minden bináris keresési lépésnél meghívódik.

        Returns:
            (optimal_quality, estimated_size) tuple
        """
        low = 1
        high = 100
        best_q = 1
        best_size = 0
        step = 0

        print(
            f"-> Optimális minőség keresése (Szigorú korlát: "
            f"max {target_size / 1024:.2f} KB)..."
        )

        while low <= high:
            mid = (low + high) // 2
            step += 1
            est_size = self.estimator.estimate_total_size(images, mid, rst_interval)
            diff = est_size - target_size

            print(
                f"   Próba Q={mid:3d} -> Becsült méret: {est_size/1024:7.2f} KB "
                f"(Eltérés: {diff/1024:+7.2f} KB)"
            )

            if progress_cb:
                progress_cb(step, mid, est_size)

            if est_size <= target_size:
                best_q = mid
                best_size = est_size
                low = mid + 1
            else:
                high = mid - 1

        if best_size == 0:
            print(
                "   [Figyelmeztetés] A kért méret annyira kicsi, hogy még Q=1 mellett "
                "sem érhető el. Q=1 lesz alkalmazva."
            )
            best_q = 1
            best_size = self.estimator.estimate_total_size(images, 1, rst_interval)

        return best_q, best_size
