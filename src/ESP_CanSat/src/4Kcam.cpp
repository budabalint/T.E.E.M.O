#include <4Kcam.h>
#include <config.h>
#include <hardware_pins.h>

Maincam::Maincam():  
    _rtspClient()
{

}

bool Maincam::begin() {
    packetBuffer = (uint8_t*)malloc(BUFFER_SIZE);
    if (packetBuffer == NULL) {
        return false;
    }
    Serial.setTxBufferSize(32768);
    Serial.begin(921600);

    pinMode(CAM_RST, OUTPUT);
    digitalWrite(CAM_RST, LOW);  delay(100);
    digitalWrite(CAM_RST, HIGH); delay(500);

    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    Ethernet.init(CAM_CS);

    if (Ethernet.begin(mac) == 0) {
        return false;
    }

    if (_rtspClient.connect(camIP, camPort)) {
        sendRTSPCommand("OPTIONS", streamURL);
        readResponse();

        sendRTSPCommand("DESCRIBE", streamURL, "Accept: application/sdp\r\n");
        readResponse();

        String setupHeaders = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
        sendRTSPCommand("SETUP", String(streamURL) + "/video", setupHeaders);
        sessionID = readResponse(true);
        
        if (sessionID != "") {
            sendRTSPCommand("PLAY", streamURL, "Session: " + sessionID + "\r\nRange: npt=0.000-\r\n");
        } else {
            return false; 
        } 
    } else {
        return false;
    }
    return true;
}

void Maincam::sendRTSPCommand(String method, String url, String extraHeaders) {
    String cmd = method + " " + url + " RTSP/1.0\r\n";
    cmd += "CSeq: " + String(CSeq++) + "\r\n";
    cmd += "User-Agent: ESP32_CanSat_Pro\r\n";
    cmd += String(authHeader) + "\r\n";
    if (extraHeaders != "") cmd += extraHeaders;
    cmd += "\r\n";
    _rtspClient.print(cmd);
}

String Maincam::readResponse(bool lookForSession) {
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
        newNAL = false;
    }
}

void Maincam::ReadAndSendImage(unsigned long timeoutMaxMs) {
    unsigned long startTime = millis();

    while ((millis() - startTime < timeoutMaxMs)) {
        if (_rtspClient.available() < 4) {
            if (_rtspClient.available() == 0) return; 
            continue; 
        }

        byte rtpHeaderBuf[4];
        _rtspClient.read(rtpHeaderBuf, 4);

        if (rtpHeaderBuf[0] == '$') {
            byte channel = rtpHeaderBuf[1];
            uint16_t packetLen = (rtpHeaderBuf[2] << 8) | rtpHeaderBuf[3];

            if (packetLen > 0 && packetLen <= BUFFER_SIZE) {
                int readLen = 0;
                unsigned long tStartRead = millis();
                
                while (readLen < packetLen && (millis() - tStartRead < 100)) {
                    int availableBytes = _rtspClient.available();
                    if (availableBytes > 0) {
                        int remainingBytes = packetLen - readLen;
                        int bytesToRead = min(availableBytes, remainingBytes);
                        _rtspClient.read(&packetBuffer[readLen], bytesToRead);
                        readLen += bytesToRead;
                    }
                }

                if (readLen == packetLen && channel == 0 && packetLen > 12) {
                    byte* payload = &packetBuffer[12];
                    int payloadLen = packetLen - 12;
                    byte nalType = (payload[0] >> 1) & 0x3F;

                    if (nalType != 38 && nalType != 35) {
                        if (nalType >= 0 && nalType <= 47) {
                            sendNALPacket(payload, payloadLen, true);
                        } 
                        else if (nalType == 49) { 
                            byte fuHeader = payload[2];
                            bool startBit = fuHeader & 0x80;
                            byte reconstructedHeader[2];
                            reconstructedHeader[0] = (payload[0] & 0x81) | ((fuHeader & 0x3F) << 1);
                            reconstructedHeader[1] = payload[1];

                            if (startBit) {
                                sendNALPacket(reconstructedHeader, 2, true);
                                sendNALPacket(&payload[3], payloadLen - 3, false);
                            } else {
                                sendNALPacket(&payload[3], payloadLen - 3, false);
                            }
                        }
                    }
                }
            } else if (packetLen > BUFFER_SIZE) {
                unsigned long tStart = millis();
                uint16_t remaining = packetLen;
                while (remaining > 0 && (millis() - tStart < 100)) {
                    int availableBytes = _rtspClient.available();
                    if (availableBytes > 0) {
                        int bytesToRead = min((int)availableBytes, (int)remaining);
                        for(int i=0; i<bytesToRead; i++) _rtspClient.read(); 
                        remaining -= bytesToRead;
                    }
                }
            }
        }
    }
}