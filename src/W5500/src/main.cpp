#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>

#define W5500_MISO  9
#define W5500_MOSI  8
#define W5500_SCK   6
#define W5500_CS    7
#define W5500_RST   5

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

IPAddress camIP(192, 168, 1, 10);
const int camPort = 554;
const char* streamURL = "rtsp://192.168.1.10/stream=1";
const char* authHeader = "Authorization: Basic cm9vdDphYWFh"; 

EthernetClient rtspClient;
String sessionID = "";
int CSeq = 1;

void sendRTSPCommand(String method, String url, String extraHeaders = "") {
  String cmd = method + " " + url + " RTSP/1.0\r\n";
  cmd += "CSeq: " + String(CSeq++) + "\r\n";
  cmd += "User-Agent: ESP32_CanSat\r\n";
  cmd += String(authHeader) + "\r\n";
  if (extraHeaders != "") cmd += extraHeaders;
  cmd += "\r\n";
  rtspClient.print(cmd);
}

String readResponse(bool lookForSession = false) {
  String response = "";
  String foundSession = "";
  long timeout = millis();
  
  while (millis() - timeout < 2000) {
    if (rtspClient.available()) {
      char c = rtspClient.read();
      response += c;
      if (response.endsWith("\r\n\r\n") && response.indexOf("Content-Length") == -1) {
        break; 
      }
    }
  }

  if (lookForSession) {
    int sIdx = response.indexOf("Session: ");
    if (sIdx != -1) {
      int endLine = response.indexOf("\r\n", sIdx);
      int endSemi = response.indexOf(";", sIdx);
      int end = endLine;
      if (endSemi != -1 && endSemi < endLine) end = endSemi;
      
      foundSession = response.substring(sIdx + 9, end);
      foundSession.trim();
    }
  }
  
  return foundSession;
}

void setup() {
  Serial.begin(4000000); 
  delay(1000);
  Serial.println("Rendszer indítása...");

  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW); delay(200);
  digitalWrite(W5500_RST, HIGH); delay(500);

  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI);
  Ethernet.init(W5500_CS);
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("DHCP Hiba! (Ellenőrizd a kábeleket)");
    while(true);
  }
  Serial.print("ESP32 IP: "); Serial.println(Ethernet.localIP());

  Serial.println("Csatlakozás a kamerához...");
  if (rtspClient.connect(camIP, camPort)) {
    Serial.println("TCP Kapcsolat OK. Handshake indítása...");

    sendRTSPCommand("OPTIONS", streamURL);
    readResponse();
    Serial.println("OPTIONS OK");

    sendRTSPCommand("DESCRIBE", streamURL, "Accept: application/sdp\r\n");
    readResponse();
    Serial.println("DESCRIBE OK");

    String setupHeaders = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
    sendRTSPCommand("SETUP", String(streamURL) + "/video", setupHeaders);
    
    sessionID = readResponse(true);
    
    if (sessionID != "") {
      Serial.println("Session ID megvan: " + sessionID);
      
      sendRTSPCommand("PLAY", streamURL, "Session: " + sessionID + "\r\nRange: npt=0.000-\r\n");
      Serial.println("PLAY parancs elküldve! Érkezik a stream...");
      Serial.println("--- INNENTŐL BINÁRIS ADAT JÖN ---");
    } else {
      Serial.println("HIBA: Nem kaptam Session ID-t a SETUP válaszban!");
      while(true);
    }

  } else {
    Serial.println("Hiba: Nem tudok csatlakozni a 192.168.1.10:554-hez.");
  }
}

byte buf[2048];

void loop() {
  int len = rtspClient.available();
  if (len > 0) {
    if (len > 2048) len = 2048;
    
    rtspClient.read(buf, len);
    Serial.write(buf, len);
  }

  if (!rtspClient.connected()) {
  }
}