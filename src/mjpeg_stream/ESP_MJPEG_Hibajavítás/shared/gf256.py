# shared/gf256.py
# =======================================================================
# Galois Field (GF256) Matematika - "God-Tier" Erasure Coding Alapja
# C++ TRANSLATION COMMENTS INCLUDED
# =======================================================================

# C++: const uint16_t GF_POLY = 0x11D;
GF_POLY = 0x11D

# C++: uint8_t gf_exp[512];
# C++: uint8_t gf_log[256];
gf_exp = [0] * 512
gf_log = [0] * 256

def init_tables():
    """
    C++: void init_tables() { ... }
    Iniciálja az exp és log táblákat a gyors szorzáshoz/osztáshoz.
    """
    x = 1
    for i in range(255):
        gf_exp[i] = x
        gf_log[x] = i
        x <<= 1
        if x & 0x100:
            x ^= GF_POLY
    
    for i in range(255, 512):
        gf_exp[i] = gf_exp[i - 255]

init_tables()

def gf_add(a: int, b: int) -> int:
    """ C++: inline uint8_t gf_add(uint8_t a, uint8_t b) { return a ^ b; } """
    return a ^ b

def gf_sub(a: int, b: int) -> int:
    """ C++: inline uint8_t gf_sub(uint8_t a, uint8_t b) { return a ^ b; } """
    return a ^ b

def gf_mul_slow(a: int, b: int) -> int:
    if a == 0 or b == 0:
        return 0
    return gf_exp[gf_log[a] + gf_log[b]]

# 256x256 LUT a villámgyors Python szorzáshoz
gf_mul_table = [[0] * 256 for _ in range(256)]
for i in range(256):
    for j in range(256):
        gf_mul_table[i][j] = gf_mul_slow(i, j)

def gf_mul(a: int, b: int) -> int:
    """ C++: inline uint8_t gf_mul(uint8_t a, uint8_t b) { ... } """
    return gf_mul_table[a][b]

def gf_div(a: int, b: int) -> int:
    """ C++: inline uint8_t gf_div(uint8_t a, uint8_t b) { ... } """
    if a == 0:
        return 0
    if b == 0:
        raise ZeroDivisionError()
    # Hozzáadunk 255-öt, hogy elkerüljük a negatív indexet
    return gf_exp[(gf_log[a] + 255 - gf_log[b]) % 255]

def gf_invert(a: int) -> int:
    """ C++: inline uint8_t gf_invert(uint8_t a) { ... } """
    if a == 0:
        raise ZeroDivisionError()
    return gf_exp[255 - gf_log[a]]

# =======================================================================
# Mátrix Műveletek GF(256) felett
# =======================================================================

def matrix_multiply(A, B):
    """
    C++: std::vector<std::vector<uint8_t>> matrix_multiply(...)
    Mátrixszorzás GF(256) mezőben.
    """
    rows_A = len(A)
    cols_A = len(A[0])
    cols_B = len(B[0])
    
    C = [[0] * cols_B for _ in range(rows_A)]
    for i in range(rows_A):
        for j in range(cols_B):
            val = 0
            for k in range(cols_A):
                val ^= gf_mul(A[i][k], B[k][j])
            C[i][j] = val
    return C

def invert_matrix(matrix):
    """
    C++: std::vector<std::vector<uint8_t>> invert_matrix(...)
    Gauss-elimináció GF(256) felett a mátrix inverzének kiszámításához.
    Visszaadja az inverz mátrixot.
    """
    n = len(matrix)
    # Kibővítjük egységmátrixszal (Augmented Matrix)
    aug = [row[:] + [1 if i == j else 0 for j in range(n)] for i, row in enumerate(matrix)]
    
    for i in range(n):
        # Keresünk egy nem nulla elemet az i. oszlopban (Pivot)
        if aug[i][i] == 0:
            for j in range(i + 1, n):
                if aug[j][i] != 0:
                    aug[i], aug[j] = aug[j], aug[i] # Sorok cseréje
                    break
            if aug[i][i] == 0:
                raise ValueError("Szinguláris mátrix, nem invertálható!")
        
        # Főátló elemét 1-re osztjuk
        pivot_inv = gf_invert(aug[i][i])
        for j in range(2 * n):
            aug[i][j] = gf_mul(aug[i][j], pivot_inv)
            
        # Kinullázzuk a többi sort
        for j in range(n):
            if i != j and aug[j][i] != 0:
                factor = aug[j][i]
                for k in range(2 * n):
                    aug[j][k] ^= gf_mul(factor, aug[i][k])
                    
    # Kivágjuk a jobb oldali részt (inverz mátrix)
    return [row[n:] for row in aug]

def build_vandermonde_matrix(rows, cols):
    """
    C++: std::vector<std::vector<uint8_t>> build_vandermonde_matrix(int rows, int cols)
    Szisztematikus Cauchy mátrix generálása (Sokkal jobb, mint a Vandermonde!)
    Garantálja az MDS (Maximum Distance Separable) tulajdonságot, vagyis 
    BÁRMELY K darab csomagból garantáltan visszaállítható az eredeti adat, 
    sosem lesz szinguláris a részmátrix!
    """
    matrix = [[0] * cols for _ in range(rows)]
    for i in range(rows):
        if i < cols:
            matrix[i][i] = 1 # Egységmátrix a nyers adatoknak
        else:
            for j in range(cols):
                # Cauchy mátrix a paritásoknak: 1 / (x_i + y_j)
                # x és y diszjunkt halmazok kell legyenek, hogy x ^ y sose legyen 0.
                x = i - cols
                y = j + (rows - cols)
                matrix[i][j] = gf_invert(x ^ y)
    return matrix
