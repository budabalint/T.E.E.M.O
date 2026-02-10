#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>

class Maincam {
public:
    Maincam();
    void sendRTSPCommand(String method, String url, String extraHeaders = "");
    void begin();
    String readResponse(bool lookForSession = false);
    void sendNALPacket(byte* data, int len, bool newNAL);

private:
    EthernetClient _rtspClient;
    String sessionID = "";
    int CSeq = 1;
};