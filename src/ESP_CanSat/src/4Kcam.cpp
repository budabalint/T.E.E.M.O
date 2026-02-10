#include <4Kcam.h>
#include <config.h>
#include <hardware_pins.h>

byte calculateCRC8(const byte *data, size_t len) {
    byte crc = 0x00;
    while (len--) {
        byte extract = *data++;
        for (byte tempI = 8; tempI; tempI--) {
        byte sum = (crc ^ extract) & 0x01;
        crc >>= 1;
        if (sum) {
            crc ^= 0x8C;
        }
        extract >>= 1;
        }
    }
    return crc;
}

Maincam::Maincam():  
    _rtspClient()
{

}

void Maincam::begin() {
    packetBuffer = (uint8_t*)malloc(BUFFER_SIZE);
    if (packetBuffer == NULL) {
        while(1) {
            delay(100);
        } 
    }
    Serial.begin(921600);
    Serial.setTxBufferSize(32768);

    pinMode(CAM_RST, OUTPUT);
    digitalWrite(CAM_RST, LOW);  delay(100);
    digitalWrite(CAM_RST, HIGH); delay(500);

    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    Ethernet.init(CAM_CS);

    if (Ethernet.begin(mac) == 0) {
        while(true) { 
            delay(1000); 
        } 
    }

    if (_rtspClient.connect(camIP, camPort)) {
        sendRTSPCommand("OPTIONS", streamURL);
        readResponse();

        sendRTSPCommand("DESCRIBE", streamURL, "Accept: application/sdp\r\n");
        readResponse();

        String setupHeaders = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
        sendRTSPCommand("SETUP", String(streamURL) + "/video", setupHeaders);
        sessionID = readResponse(true); // GET Session id
        
        if (sessionID != "") {
        sendRTSPCommand("PLAY", streamURL, "Session: " + sessionID + "\r\nRange: npt=0.000-\r\n");
        } else {
        while(true){ 
            delay(1000); 
        } 
        }
    } else {
        while(true) { 
            delay(1000);
        } 
    }
}

void Maincam::sendRTSPCommand(String method, String url, String extraHeaders = "") {
    String cmd = method + " " + url + " RTSP/1.0\r\n";
    cmd += "CSeq: " + String(CSeq++) + "\r\n";
    cmd += "User-Agent: ESP32_CanSat_Pro\r\n";
    cmd += String(authHeader) + "\r\n";
    if (extraHeaders != "") cmd += extraHeaders;
    cmd += "\r\n";
    _rtspClient.print(cmd);
}

String Maincam::readResponse(bool lookForSession = false) {
    String response = "";
    String foundSession = "";
    unsigned long timeout = millis();
    
    while (millis() - timeout < 2000) {
        if (_rtspClient.available()) {
        char c = _rtspClient.read();
        response += c;
        if (response.endsWith("\r\n\r\n")) break;
        }
    }

    if (lookForSession) {
        int sIdx = response.indexOf("Session: ");
        if (sIdx != -1) {
        int endLine = response.indexOf("\r\n", sIdx);
        foundSession = response.substring(sIdx + 9, endLine);
        foundSession.trim();
        if (foundSession.indexOf(';') != -1) {
            foundSession = foundSession.substring(0, foundSession.indexOf(';'));
        }
        }
    }
    return foundSession;
}

void Maincam::sendNALPacket(byte* data, int len, bool newNAL) {
    static byte seq = 0; 
    int offset = 0;

    while (offset < len) {
        int chunkLen = min(30, len - offset);

        byte payload[30];
        memset(payload, 0, 30);
        memcpy(payload, &data[offset], chunkLen);

        byte start_flag = (newNAL ? 0x80 : 0x00);         // Bit 7: Start Flag
        byte length_info = (byte)(chunkLen & 0x1F) << 2;  // Bit 6-2: Hossz (5 bit, 0-31), a helyére tolva
        byte seq_info = (byte)(seq & 0x03);               // Bit 1-0: Szekvencia (2 bit, 0-3)

        byte header = start_flag | length_info | seq_info;

        seq = (seq + 1) & 0x03;

        byte currentCRC = calculateCRC8(payload, 30);

        Serial.write(header);      // 1 bájt Header
        Serial.write(payload, 30); // 30 bájt Payload (adat + padding)
        Serial.write(currentCRC);  // 1 bájt CRC

        offset += chunkLen;
        newNAL = false; // Csak az első darab új NAL
    }
}
