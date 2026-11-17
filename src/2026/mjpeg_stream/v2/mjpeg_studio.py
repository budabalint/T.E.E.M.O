"""
mjpeg_studio.py
===============
MJPEG Studio belépési pontja.
"""

import sys
import os

from PyQt6.QtWidgets import QApplication
from studio.main_window import MainWindow

def main():
    app = QApplication(sys.argv)
    
    # Stílusok globális beállítása a sötét témához
    app.setStyle("Fusion")
    
    window = MainWindow()
    window.show()
    
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
