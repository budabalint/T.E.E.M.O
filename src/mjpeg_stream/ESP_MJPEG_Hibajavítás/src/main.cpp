#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "fec_codec.h"

// --- Protocol Constants ---
constexpr int PACKET_SIZE = 127;
constexpr int PAYLOAD_SIZE = 121;
constexpr uint8_t SYNC_BYTE = 0xFE;
constexpr uint8_t TYPE_MJPEG = 0xDD;
constexpr uint8_t TYPE_FEC = 0xFF;

// UEP Configuration
constexpr int HEADER_K = 10;
constexpr int HEADER_M = 10;
constexpr int DATA_K = 20;
constexpr int DATA_M = 12;

constexpr int INTERLEAVE_GROUP = 20;

// Structs
struct UepConfig {
    int K, M, N;
    UepConfig(int k, int m) : K(k), M(m), N(k + m) {}
};

struct Chunk {
    int id;
    std::string type;
    std::vector<uint8_t> raw_data;
    UepConfig cfg;
    fec::FecCodec* codec;
};

// --- CRC8 ---
uint8_t calc_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = ((crc << 1) ^ 0x07) & 0xFF;
            } else {
                crc = (crc << 1) & 0xFF;
            }
        }
    }
    return crc;
}

// --- UepChunker ---
class UepChunker {
public:
    UepChunker() 
        : header_cfg(HEADER_K, HEADER_M), data_cfg(DATA_K, DATA_M),
          header_codec(HEADER_K, HEADER_M), data_codec(DATA_K, DATA_M) {}

    std::vector<Chunk> chunk_frame(const uint8_t* frame_bytes, size_t frame_size) {
        std::vector<Chunk> chunks;
        
        size_t hdr_size = header_cfg.K * PAYLOAD_SIZE;
        size_t hdr_copy_len = std::min(frame_size, hdr_size);
        std::vector<uint8_t> hdr_bytes(frame_bytes, frame_bytes + hdr_copy_len);

        chunks.push_back({
            0, "HEADER", hdr_bytes, header_cfg, &header_codec
        });

        size_t dat_start = hdr_copy_len;
        size_t dat_size = frame_size - dat_start;
        
        size_t blk_size = data_cfg.K * PAYLOAD_SIZE;
        int num_data_chunks = dat_size == 0 ? 0 : std::ceil(static_cast<float>(dat_size) / blk_size);

        for (int i = 0; i < num_data_chunks; i++) {
            size_t start = dat_start + i * blk_size;
            size_t end = std::min(start + blk_size, frame_size);
            std::vector<uint8_t> slice_b(frame_bytes + start, frame_bytes + end);
            chunks.push_back({
                i + 1, "DATA", slice_b, data_cfg, &data_codec
            });
        }

        return chunks;
    }

private:
    UepConfig header_cfg;
    UepConfig data_cfg;
    fec::FecCodec header_codec;
    fec::FecCodec data_codec;
};

// --- PacketEncoder ---
class PacketEncoder {
public:
    std::vector<uint8_t> create_packet(int frame_id, int chunk_id, int pkt_idx, int packet_type, const uint8_t* payload, size_t payload_len) {
        std::vector<uint8_t> body;
        body.reserve(PACKET_SIZE);
        body.push_back(SYNC_BYTE);
        body.push_back(packet_type & 0xFF);
        body.push_back(frame_id & 0xFF);
        body.push_back(chunk_id & 0xFF);
        body.push_back(pkt_idx & 0xFF);
        
        size_t copy_len = std::min((size_t)PAYLOAD_SIZE, payload_len);
        body.insert(body.end(), payload, payload + copy_len);
        
        // Zero-padding if payload is shorter
        while (body.size() < (size_t)(5 + PAYLOAD_SIZE)) {
            body.push_back(0x00);
        }
        
        uint8_t crc = calc_crc8(body.data(), body.size());
        body.push_back(crc);
        return body;
    }
};

// --- Interleaver ---
class Interleaver {
public:
    Interleaver(int group_size = INTERLEAVE_GROUP) : group_size_(group_size) {}

    void transmit_stream(int frame_id, std::vector<Chunk>& chunks, PacketEncoder& encoder) {
        for (size_t grp_start = 0; grp_start < chunks.size(); grp_start += group_size_) {
            size_t grp_end = std::min(grp_start + group_size_, chunks.size());
            
            struct EncodedGroup {
                int id;
                std::vector<uint8_t> data;
                std::vector<uint8_t> parity;
                int K;
                int M;
            };
            std::vector<EncodedGroup> encoded_grp;
            size_t max_pkts = 0;

            for (size_t i = grp_start; i < grp_end; i++) {
                auto& chunk = chunks[i];
                auto parity = chunk.codec->encode(chunk.raw_data.data(), chunk.raw_data.size(), PAYLOAD_SIZE);
                
                encoded_grp.push_back({chunk.id, chunk.raw_data, parity, chunk.cfg.K, chunk.cfg.M});
                max_pkts = std::max(max_pkts, (size_t)(chunk.cfg.K + chunk.cfg.M));
            }

            for (size_t pkt_idx = 0; pkt_idx < max_pkts; pkt_idx++) {
                for (auto& ec : encoded_grp) {
                    if (pkt_idx < (size_t)(ec.K + ec.M)) {
                        bool is_fec = (pkt_idx >= static_cast<size_t>(ec.K));
                        int packet_type = is_fec ? TYPE_FEC : TYPE_MJPEG;
                        
                        std::vector<uint8_t> pkt;
                        if (is_fec) {
                            size_t p_idx = pkt_idx - ec.K;
                            size_t offset = p_idx * PAYLOAD_SIZE;
                            pkt = encoder.create_packet(frame_id, ec.id, pkt_idx, packet_type, ec.parity.data() + offset, PAYLOAD_SIZE);
                        } else {
                            size_t offset = pkt_idx * PAYLOAD_SIZE;
                            size_t remain = (offset < ec.data.size()) ? (ec.data.size() - offset) : 0;
                            pkt = encoder.create_packet(frame_id, ec.id, pkt_idx, packet_type, ec.data.data() + offset, remain);
                        }
                        Serial.write(pkt.data(), pkt.size());
                    }
                }
            }
        }
    }
private:
    int group_size_;
};

// --- Global State ---
uint8_t* g_psram_buf = nullptr;
size_t g_file_size = 0;
std::vector<size_t> g_frame_offsets;
uint8_t* g_processed_frame = nullptr; // 1MB pre-allocated PSRAM buffer for the current frame
UepChunker* g_chunker = nullptr;
PacketEncoder* g_encoder = nullptr;
Interleaver* g_interleaver = nullptr;

// --- RST Injection ---
size_t inject_rst_table(const uint8_t* frame_data, size_t frame_size, uint8_t* out_buf) {
    auto find_marker = [](const uint8_t* data, size_t size, uint8_t m1, uint8_t m2, size_t start = 0) -> int {
        for (size_t i = start; i < size - 1; i++) {
            if (data[i] == m1 && data[i+1] == m2) return i;
        }
        return -1;
    };

    int sos_idx = find_marker(frame_data, frame_size, 0xFF, 0xDA);
    if (sos_idx == -1) {
        memcpy(out_buf, frame_data, frame_size);
        return frame_size;
    }

    int sos_len = (frame_data[sos_idx + 2] << 8) + frame_data[sos_idx + 3];
    size_t scan_start = sos_idx + 2 + sos_len;
    std::vector<size_t> rst_positions;
    
    size_t idx = scan_start;
    while (idx < frame_size - 1) {
        if (frame_data[idx] == 0xFF) {
            uint8_t marker = frame_data[idx + 1];
            if (marker >= 0xD0 && marker <= 0xD7) {
                rst_positions.push_back(idx);
            } else if (marker == 0xD9) { // EOI
                break;
            }
            idx += 2;
        } else {
            idx++;
        }
    }

    if (rst_positions.empty()) {
        memcpy(out_buf, frame_data, frame_size);
        return frame_size;
    }

    size_t hdr_bytes = HEADER_K * PAYLOAD_SIZE;
    size_t dat_bytes = DATA_K * PAYLOAD_SIZE;
    size_t fs = frame_size;
    
    int n_total = 1;
    int app3_marker_size = 0;
    
    for (int it = 0; it < 3; it++) {
        size_t remaining = (fs > hdr_bytes) ? (fs - hdr_bytes) : 0;
        int n_data = remaining > 0 ? std::ceil(static_cast<float>(remaining) / dat_bytes) : 0;
        n_total = 1 + n_data;
        int app3_data_len = 4 + 2 + n_total; // 'RST\0' + total(2B) + counts
        app3_marker_size = 2 + 2 + app3_data_len; // FF E3 + len(2B) + data
        size_t new_size = frame_size + app3_marker_size;
        if (new_size == fs) break;
        fs = new_size;
    }

    std::vector<size_t> shifted;
    for (size_t p : rst_positions) shifted.push_back(p + app3_marker_size);

    std::vector<int> rst_per_chunk(n_total, 0);
    for (size_t pos : shifted) {
        if (pos < hdr_bytes) {
            rst_per_chunk[0]++;
        } else {
            int cid = 1 + (pos - hdr_bytes) / dat_bytes;
            if (cid < n_total) {
                rst_per_chunk[cid]++;
            }
        }
    }

    std::vector<uint8_t> app3;
    app3.push_back(0xFF);
    app3.push_back(0xE3);
    
    int payload_len = 4 + 2 + n_total;
    app3.push_back((payload_len + 2) >> 8);
    app3.push_back((payload_len + 2) & 0xFF);
    
    app3.push_back('R'); app3.push_back('S'); app3.push_back('T'); app3.push_back('\0');
    app3.push_back((rst_positions.size() >> 8) & 0xFF);
    app3.push_back(rst_positions.size() & 0xFF);
    
    for (int c : rst_per_chunk) {
        app3.push_back(std::min(c, 255));
    }

    size_t out_idx = 0;
    memcpy(out_buf + out_idx, frame_data, 2); out_idx += 2;
    memcpy(out_buf + out_idx, app3.data(), app3.size()); out_idx += app3.size();
    memcpy(out_buf + out_idx, frame_data + 2, frame_size - 2); out_idx += frame_size - 2;
    
    return out_idx;
}

void load_frames_from_file(File& file) {
    g_file_size = file.size();
    Serial.printf("File size: %d bytes\n", g_file_size);
    
    g_psram_buf = (uint8_t*)ps_malloc(g_file_size);
    if (!g_psram_buf) {
        Serial.println("Failed to allocate PSRAM for file!");
        return;
    }
    file.read(g_psram_buf, g_file_size);

    size_t idx = 0;
    auto find_soi = [&](size_t start) -> int {
        for (size_t i = start; i < g_file_size - 1; i++) {
            if (g_psram_buf[i] == 0xFF && g_psram_buf[i+1] == 0xD8) return i;
        }
        return -1;
    };

    while (idx < g_file_size) {
        int soi = find_soi(idx);
        if (soi == -1) break;
        
        g_frame_offsets.push_back(soi);
        
        int next_soi = find_soi(soi + 2);
        if (next_soi == -1) {
            // Last frame: no further SOI found, stop here
            break;
        }
        idx = next_soi;

        // Yield to avoid Watchdog Timer reset during long scans
        if (g_frame_offsets.size() % 50 == 0) {
            yield();
        }
    }
    // Add end of file offset so that frame_len = g_frame_offsets[i+1] - g_frame_offsets[i]
    g_frame_offsets.push_back(g_file_size);
    
    Serial.printf("Found %d frame(s). Free PSRAM: %d bytes\n",
                  (int)(g_frame_offsets.size() > 0 ? g_frame_offsets.size() - 1 : 0),
                  ESP.getFreePsram());
}

void setup() {
    Serial.begin(921600); // 921600 baud rate as required
    
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    File file = LittleFS.open("/stream.mjpeg", "r");
    if (!file) {
        Serial.println("Failed to open stream.mjpeg");
        return;
    }

    Serial.println("Loading frames...");
    load_frames_from_file(file);
    file.close();
    Serial.printf("Found %d frames.\n", g_frame_offsets.size() > 0 ? g_frame_offsets.size() - 1 : 0);

    g_processed_frame = (uint8_t*)ps_malloc(1024 * 1024); // 1MB buffer for processing frames
    if (!g_processed_frame) {
        Serial.println("Failed to allocate PSRAM for processed frame!");
        return;
    }

    g_chunker = new UepChunker();
    g_encoder = new PacketEncoder();
    g_interleaver = new Interleaver(INTERLEAVE_GROUP);
    
    Serial.println("Initialization complete. Starting transmission.");
}

// Target frame rate: 30 FPS → ~33 ms between frames
// Adjust this value to match your desired playback speed.
constexpr unsigned long FRAME_INTERVAL_MS = 33;

void loop() {
    if (g_frame_offsets.size() < 2 || !g_psram_buf || !g_processed_frame) {
        delay(1000);
        return;
    }

    static int frame_idx = 0;
    static unsigned long last_frame_time = 0;

    // Wait until it is time to send the next frame
    unsigned long now = millis();
    if (now - last_frame_time < FRAME_INTERVAL_MS) {
        return; // Yield CPU; will be called again immediately by the Arduino scheduler
    }
    last_frame_time = now;

    size_t start_offset = g_frame_offsets[frame_idx];
    size_t end_offset   = g_frame_offsets[frame_idx + 1];
    size_t frame_len    = end_offset - start_offset;
    
    // Inject RST table and get the final frame length
    size_t processed_len = inject_rst_table(g_psram_buf + start_offset, frame_len, g_processed_frame);
    
    // Chunk → FEC encode → Interleave → Transmit over Serial
    std::vector<Chunk> chunks = g_chunker->chunk_frame(g_processed_frame, processed_len);
    
    // Frame ID wraps 0-255
    static int protocol_frame_id = 0;
    g_interleaver->transmit_stream(protocol_frame_id, chunks, *g_encoder);
    Serial.flush(); // Ensure all bytes are sent before advancing to next frame
    
    protocol_frame_id = (protocol_frame_id + 1) % 256;
    
    frame_idx++;
    if (frame_idx >= (int)(g_frame_offsets.size() - 1)) {
        frame_idx = 0; // Loop the video
    }
}