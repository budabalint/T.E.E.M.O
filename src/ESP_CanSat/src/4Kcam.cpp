#include <4Kcam.h>
#include <config.h>
#include <hardware_pins.h>


Maincam::Maincam():  
    _rtspClient()
{


}

bool Maincam::checkW5500() {
    Serial.println("W5500 hardver tesztelése...");
    
    pinMode(CAM_RST, OUTPUT);
    digitalWrite(CAM_RST, LOW);  delay(100);
    digitalWrite(CAM_RST, HIGH); delay(500);

    Ethernet.init(CAM_CS);

    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Kritikus Hiba: W5500 modul nem válaszol az SPI buszon!");
        return false;
    }

    if (Ethernet.hardwareStatus() == EthernetW5500) {
        Serial.println("Sikeres W5500 SPI kommunikáció!");
    }

    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Figyelmeztetés: Nincs hálózati kábel csatlakoztatva a W5500-hoz!");
    } else {
        Serial.println("Hálózati kábel csatlakoztatva (Link UP)!");
    }

    return true;
}

bool Maincam::begin() {
    packetBuffer = (uint8_t*)malloc(BUFFER_SIZE);
    if (packetBuffer == NULL) {
        return false;
    }
    //Serial.setTxBufferSize(32768);

    //SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);

    if (!checkW5500()) {
        return false; // Ha nincs SPI kapcsolat, itt megállunk
    }

    IPAddress localIP(192, 168, 0, 100);    // ESP32 IP címe
    IPAddress gateway(192, 168, 0, 1);      // Router címe
    IPAddress subnet(255, 255, 255, 0);     // Alalhálózati maszk
    IPAddress dns(8, 8, 8, 8);              // DNS
    
    Serial.println("W5500 inicializálása statikus IP-vel...");
    Ethernet.begin(mac, localIP, dns, gateway, subnet);
    delay(200); // Kicsi pihenő a hálózati rétegnek

    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Mivel nincs link, az RTSP csatlakozás ki lesz hagyva.");
        return true; // Sikeres init, de kamera nélkül futunk tovább
    }

    Serial.println("Csatlakozás a kamerához...");
    if (_rtspClient.connect(camIP, camPort)) {
        sendRTSPCommand("OPTIONS", streamURL);
        readResponse(false);

        sendRTSPCommand("DESCRIBE", streamURL, "Accept: application/sdp\r\n");
        readResponse(false);

        String setupHeaders = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
        sendRTSPCommand("SETUP", String(streamURL) + "/video", setupHeaders);
        sessionID = readResponse(true);
        
        if (sessionID != "") {
            sendRTSPCommand("PLAY", streamURL, "Session: " + sessionID + "\r\nRange: npt=0.000-\r\n");
            Serial.println("RTSP Stream elindítva!");
        } else {
            Serial.println("Hiba: Nincs Session ID");
            return false; 
        } 
    } else {
        Serial.println("Hiba: RTSP Socket csatlakozás sikertelen");
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
    
    const int PACKET_SIZE = 255;
    const int MAX_PAYLOAD = 251; // 255 - ID - Seq - LengthByte - CRC = 251

    while (offset < len) {
        int chunkLen = min(MAX_PAYLOAD, len - offset);

        byte packetBuffer[PACKET_SIZE];
        memset(packetBuffer, 0, PACKET_SIZE); // Feltöltjük nullákkal (padding)

        if (newNAL && offset == 0) {
            packetBuffer[0] = 0xC1; // Új NAL frame kezdete
        } else {
            packetBuffer[0] = 0xC0; // Előző NAL folytatása
        }

        packetBuffer[1] = seq;

        packetBuffer[2] = (byte)chunkLen;

        memcpy(&packetBuffer[3], &data[offset], chunkLen);
        byte currentCRC = calculateCRC8(packetBuffer, PACKET_SIZE - 1);

        packetBuffer[PACKET_SIZE - 1] = currentCRC;

        Serial.write(packetBuffer, PACKET_SIZE);

        seq++;
        offset += chunkLen;
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
