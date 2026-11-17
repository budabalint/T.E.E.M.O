# shared/fec_codec.py
# =======================================================================
# FEC (Forward Error Correction) Mátrix Kódoló és Dekódoló
# C++ TRANSLATION COMMENTS INCLUDED
# =======================================================================
import sys
import os

# Adjuk hozzá a shared mappát az eléréshez
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from gf256 import build_vandermonde_matrix, matrix_multiply, invert_matrix, gf_add, gf_mul

class FecCodec:
    """
    C++: class FecCodec { ... };
    Reed-Solomon / Matrix Erasure alapú FEC kodek.
    Lehetővé teszi 'K' adatcsomagból 'M' paritáscsomag generálását,
    majd bármely 'K' darab (adat vagy paritás) csomagból az eredeti adatok visszaállítását.
    """
    
    def __init__(self, data_count: int, parity_count: int):
        """
        C++: FecCodec(int data_count, int parity_count);
        """
        self.K = data_count
        self.M = parity_count
        self.N = data_count + parity_count
        
        # Generátor mátrix (N x K)
        self.G = build_vandermonde_matrix(self.N, self.K)

    def encode(self, data_packets: list) -> list:
        """
        C++: std::vector<std::vector<uint8_t>> encode(const std::vector<std::vector<uint8_t>>& data_packets);
        Legenerálja az összes paritáscsomagot.
        data_packets: K darab azonos hosszúságú (pl. 122 bájt) bytearray.
        Returns: M darab paritáscsomag.
        """
        if len(data_packets) != self.K:
            raise ValueError(f"Pontosan {self.K} adatcsomag szükséges!")
            
        packet_len = len(data_packets[0])
        parity_packets = [bytearray(packet_len) for _ in range(self.M)]
        
        # Mátrixszorzás: Parity = G[K:] * Data
        for p_idx in range(self.M):
            row_in_G = self.K + p_idx
            for byte_idx in range(packet_len):
                val = 0
                for d_idx in range(self.K):
                    val ^= gf_mul(self.G[row_in_G][d_idx], data_packets[d_idx][byte_idx])
                parity_packets[p_idx][byte_idx] = val
                
        return parity_packets

    def decode(self, received_packets: list, received_indices: list, packet_len: int) -> list:
        """
        C++: std::vector<std::vector<uint8_t>> decode(const std::vector<std::vector<uint8_t>>& received_packets, const std::vector<int>& received_indices, int packet_len);
        Visszaállítja a K darab eredeti adatcsomagot.
        received_packets: Legalább K darab érvényes csomag (adat vagy paritás vegyesen).
        received_indices: A kapott csomagok eredeti indexei (0-tól N-1-ig).
        """
        if len(received_packets) < self.K:
            raise ValueError(f"Nincs meg a szükséges K={self.K} csomag a dekódoláshoz!")
            
        # Csak K darab csomagot használunk fel a mátrix inverzióhoz
        used_packets = received_packets[:self.K]
        used_indices = received_indices[:self.K]
        
        # Összeállítjuk a G mátrix megfelelő K x K-s részét
        sub_G = []
        for idx in used_indices:
            sub_G.append(self.G[idx])
            
        # Inverz mátrix kiszámítása
        inv_G = invert_matrix(sub_G)
        
        # Eredeti adatok visszaállítása (Data = inv_G * UsedPackets)
        recovered_data = [bytearray(packet_len) for _ in range(self.K)]
        
        for r_idx in range(self.K): # Recovered index
            for byte_idx in range(packet_len):
                val = 0
                for c_idx in range(self.K): # Column in inv_G / Packet index in used_packets
                    val ^= gf_mul(inv_G[r_idx][c_idx], used_packets[c_idx][byte_idx])
                recovered_data[r_idx][byte_idx] = val
                
        return recovered_data
