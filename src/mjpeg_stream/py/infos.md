Ez a technikai kiegészítés a `makemjpeg.py` parancssori eszköz **részletes kézikönyvét és belső architektúrájának leírását** tartalmazza. A dokumentáció lépésről lépésre bemutatja a paraméterezést, a háttérben futó matematikai optimalizációt, a fájlrendszer-követelményeket és a hibaelhárítási folyamatokat.

---

# `makemjpeg.py` – Technikai Kézikönyv és Részletes Súgó

Ez az eszköz egy intelligens, bináris keresésen alapuló rate-control (méretvezérelt) kódoló és MJPEG csomagoló. Segítségével tetszőleges forrásképekből állítható elő szabványos, de a redundáns tábláktól megfosztott, szigorúan méretre szabott `.mjpeg` adatfolyam.

---

## 1. Előfeltételek és Telepítés Windows Rendszeren

A program futtatásához két külső komponens szükséges:

### A. Python és a Pillow könyvtár
A képek beolvasásához, RGB színtér-konverziójához és 720p (1280x720) felbontásra skálázásához a `Pillow` (PIL) könyvtár szükséges.
Telepítése parancssorból:
```cmd
pip install Pillow
```

### B. FFmpeg integráció
A program az átmeneti képek veszteségmentes összefűzéséhez az `ffmpeg` parancssori eszközt hívja meg a háttérben.
1. Töltsd le az FFmpeg statikus buildjét (pl. a gyanSHEV vagy Biboffice buildeket).
2. Az `ffmpeg.exe` fájlt helyezd el:
   * **VAGY** a Windows `System32` mappájába,
   * **VAGY** egy tetszőleges mappába, aminek az elérési útját hozzáadod a Windows környezeti változóihoz (`PATH`),
   * **VAGY** egyszerűen másold be az `ffmpeg.exe`-t közvetlenül a `makemjpeg.py` mellé.

---

## 2. A Könyvtárszerkezet Előkészítése

A program indítása előtt az alábbi könyvtárszerkezet javasolt a szkript környezetében:

```text
[Projekt Mappa]
  │
  ├── makemjpeg.py                # Maga a futtatható Python szkript
  │
  ├── input/                      # Bemeneti mappa (bármilyen nevű képekkel)
  │     ├── dsc_001.png
  │     ├── vacsorafoto.jpg
  │     └── ...
  │
  └── images/                     # Átmeneti mappa (a program automatikusan kezeli)
        ├── 1.jpeg
        ├── 2.jpeg
        └── ...
```

*Megjegyzés: Az `input` mappában lévő képek nevei bármik lehetnek (nem szükséges a számozás). A program a fájlok ABC-sorrendje alapján fogja őket feldolgozni és sorszámozni.*

---

## 3. Parancssori Argumentumok és Szintaxis

A szkript a standard `argparse` könyvtárra épül. A részletes súgót a következő paranccsal hívhatod meg:
```cmd
python make_mjpeg.py --help
```

### Részletes paraméter-lista:

| Kapcsoló (Rövid / Hosszú) | Alapértelmezett érték | Típus | Leírás |
| :--- | :--- | :--- | :--- |
| `-i`, `--input` | `"input"` | Sztring | A forrásképeket tartalmazó könyvtár elérési útja. |
| `-o`, `--output` | `"stream_abbreviated.mjpeg"` | Sztring | A legenerált, optimalizált MJPEG fájl neve és helye. |
| `-t`, `--target-size` | *Nincs megadva* | Sztring | A kívánt kimeneti fájlméret. Támogatja a `K` (Kilobájt), `M` (Megabájt) utótagokat és a nyers bájtokat is (pl. `500K`, `1.5M`, `2000000`). |
| `-q`, `--quality` | `85` | Egész szám | Fix minőségi tényező (1-100), amelyet akkor alkalmaz, ha **nincs** megadva célméret. |

---

## 4. Részletes Működési Módok és Példák

### A. "Okos" Rate-Control Mód (Méretre tömörítés)
Ebben a módban a program megbecsüli és megkeresi azt az optimális minőségi tényezőt ($Q$), amivel a fájl eléri a kért méretet.

**Példa 1: Képek tömörítése maximum 300 Kilobájtos adatfolyammá**
```cmd
python makemjpeg.py --input input --output nyaralas_300k.mjpeg --target-size 300K
```
A program beolvassa az `input/` tartalmát, átméretezi őket 720p-re, kiszámolja a Huffman-táblák elhagyása utáni valós bájtokat, majd addig finomítja a minőséget bináris kereséssel, amíg a kimenet a lehető legközelebb nem kerül a 300 KB-hoz.

**Példa 2: Szigorú, bájt-pontos korlát beállítása (pl. hálózati puffer-mérethez)**
Ha pontosan 1 500 000 bájtot szeretnél elérni:
```cmd
python makemjpeg.py -i input -o buffer_test.mjpeg -t 1500000
```

---

### B. Fix Minőségű Mód (Méretkorlát nélkül)
Ha nem fontos a fájlméret, csak egy egységes, jó minőségű, de a felesleges tábláktól megtisztított folyamot szeretnél.

**Példa: Átméretezés 720p-re, fix 75-ös minőséggel történő kódolás és táblamegvonás:**
```cmd
python makemjpeg.py -i input -o fix_minoseg.mjpeg -q 75
```

---

### C. Egyszerű Összefűző Mód (Hagyományos `makemjpeg` parancs)
Ha a képek már eleve elő vannak készítve, megfelelően át vannak méretezve, és sorszámozva fekszenek az `images/` mappában (`1.jpeg`, `2.jpeg`... stb.), a program képes az előfeldolgozást kihagyva közvetlenül ezekből dolgozni.

Ehhez egyszerűen töröld le az `input` mappát (vagy nevezd át), és futtasd paraméterek nélkül a programot:
```cmd
python makemjpeg.py
```
A program detektálja, hogy az `input/` üres vagy hiányzik, de az `images/` mappában ott vannak a kész képek. Azonnal meghívja az FFmpeg-et, összefűzi őket, és elvégzi a fejléc-eltávolítást.

---

## 5. A Háttérben Futó Technológiák Mélyreható Bemutatása

### A. A táblamegvonás (Header Stripping) mechanizmusa
A JPEG képekben az információk szegmensekben vannak elhelyezve, amelyeket `FF XX` markerek vezetnek be. Az `MjpegPacker` és a `BitrateEstimator` osztályok bájtról bájtra vizsgálják ezt a struktúrát:
1. Az **1. képkocka** teljesen érintetlen marad (tartalmazza az összes fejlécet, kvantálási és Huffman-táblát).
2. A **2. képkockától kezdve** a program elkezdi beolvasni a bájtokat. Ha a következő markerek egyikével találkozik, a hozzájuk tartozó adatblokkot (hosszuk alapján kiszámolva) egyszerűen átugorja és nem írja bele a kimenetbe:
   * `FF E0` (APP0 – JFIF metaadatok)
   * `FF DB` (DQT – Kvantálási táblák)
   * `FF C4` (DHT – Huffman-táblák)
3. Az `FF C0` (SOF0 – Geometria) és az `FF DA` (SOS – Start of Scan) szegmenseket kötelezően megtartja, mivel ezek tartalmazzák a kép 1280x720-as méretét és a képadatok kezdetét.

### B. Miért gyors az okos keresési hurok?
Ha a program minden egyes próba-minőséghez lemezre írná a képeket és meghívná az FFmpeg-et, az optimalizáció akár percekig is eltarthatna. Ehelyett a `BitrateEstimator` a **számítógép memóriájában** (`io.BytesIO`) szimulálja a tömörítést és a fenti fejléc-eltávolítást. Egy próbakör futási ideje így mindössze néhány ezredmásodperc, a teljes optimális $Q$ megtalálása pedig kevesebb mint egy másodpercet vesz igénybe.

### C. A bináris keresés menete a konzolon (Példa)
Ha elindítod a programot egy 400 KB-os célmérettel (`-t 400K`), a konzolon az alábbihoz hasonló folyamat látható:

```text
-> Images beolvasása és 720p-re méretezése...
-> Optimális minőség keresése a 400.00 KB-os célhoz...
   Próba Q= 50 -> Becsült méret:  280.12 KB (Eltérés: -119.88 KB)
   Próba Q= 75 -> Becsült méret:  415.50 KB (Eltérés:  +15.50 KB)
   Próba Q= 62 -> Becsült méret:  350.20 KB (Eltérés:  -49.80 KB)
   Próba Q= 68 -> Becsült méret:  382.10 KB (Eltérés:  -17.90 KB)
   Próba Q= 71 -> Becsült méret:  398.80 KB (Eltérés:   -1.20 KB)
   Próba Q= 73 -> Becsült méret:  410.20 KB (Eltérés:  +10.20 KB)
   Próba Q= 72 -> Becsült méret:  404.50 KB (Eltérés:   +4.50 KB)
-> Kiválasztott optimális minőség: Q=71 (becsült méret: 398.80 KB)
-> Stream csomagolása és optimalizálása...
-> Kimeneti fájl sikeresen mentve ide: stream_abbreviated.mjpeg (398.80 KB)
```

---

## 6. Hibaelhárítás és Korlátok

### 1. "Az FFmpeg nem található" hibaüzenet
*   **Ok:** A Windows nem tudja, hol van az `ffmpeg.exe`.
*   **Megoldás:** Töltsd le az `ffmpeg.exe`-t, és másold be közvetlenül a `makemjpeg.py` fájl mellé az aktuális mappába.

### 2. A generált fájl mérete eltér a kért $N$ mérettől
*   **Ok:** A JPEG tömörítés nem folytonos, hanem diszkrét lépésekben (1-től 100-ig terjedő egész számú minőségi skálán) változtatható. Előfordulhat, hogy pl. $Q=71$ mellett a méret 398 KB, de $Q=72$ mellett már 404 KB lenne. A program ilyenkor kiválasztja a célhoz legközelebbi értéket (a fenti példában a 398 KB-ot).
*   **Extrém esetek:** 
    *   Ha túl kicsi méretet adsz meg (pl. `-t 10K` 30 darab 720p-s képhez), a program $Q=1$ minőségre áll be, de a fájl még így is nagyobb lesz, mint a kért érték.
    *   Ha túl nagy méretet adsz meg (pl. `-t 50M`), a program megáll $Q=100$-nál.

### 3. Az FFmpeg hibaüzenetet ad a képkockák összefűzésekor
*   **Ok:** A bemeneti képek számozása hibás, vagy nem `1`-gyel kezdődik.
*   **Megoldás:** Mindig az `input` mappába tedd a képeket, és hagyd, hogy a program saját maga végezze el az átméretezést és a számozást az `images/` mappába. Ne nyúlj bele kézzel az `images/` mappába a folyamat futása alatt.