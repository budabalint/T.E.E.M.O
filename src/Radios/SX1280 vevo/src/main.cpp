/*
 * ESP32-S3 VEVŐ (Receiver) - SX1280 + OLED + NeoPixel LED
 * SX1280 rádión fogadja a 126 bájtos adatcsomagokat,
 * hozzáfűzi az RSSI-t, továbbítja 127 bájtként a soros porton,
 * és kezeli az OLED kijelzőt, illetve a WS2812B LED sort a jelerősség alapján.
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// --- SX1280 PINOK ---
#define SPI_SCK       12
#define SPI_MOSI      11
#define SPI_MISO      9
#define SX1280_NSS    14
#define SX1280_DIO1   20
#define SX1280_NRST   40
#define SX1280_BUSY   13

SPIClass customSPI(FSPI);
Module* module = new Module(SX1280_NSS, SX1280_DIO1, SX1280_NRST, SX1280_BUSY, customSPI);
SX1280 radio(module);

#define PACKET_LEN 126
#define SERIAL_LEN 127
#define SYNC_BYTE  0xFE
#define RSSI_UPDATE_INTERVAL 50

uint8_t videoPacket[PACKET_LEN];
volatile bool receivedFlag = false;

// --- OLED ÉS LED PINOK ÉS BEÁLLÍTÁSOK ---
const uint8_t SDA_PIN = 21;
const uint8_t SCL_PIN = 47;

#define LED_COUNT   8
#define LED_PIN     48

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- STATISZTIKA ÉS RSSI VÁLTOZÓK ---
uint32_t totalPackets = 0;        // Ez volt a packetCounter
unsigned long crcErrors = 0;
int8_t lastRssi = 0;              // cache-elt utolsó RSSI érték

unsigned long lastSignalTime = 0;
const unsigned long signalTimeout = 1000;

// CRC-8 Kalkulátor
uint8_t calcCrc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t tempI = 8; tempI; tempI--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) {
                crc ^= 0x8C;
            }
            extract >>= 1;
        }
    }
    return crc;
}

#if defined(ESP8266) || defined(ESP32)
  IRAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

void setupRadio() {
  Serial.println("--- SX1280 Inicializalasa folyamatban... ---");
  
  int state = radio.beginFLRC(2440.0, 1300, 2, 0, 16, RADIOLIB_SHAPING_0_5);
  
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Hiba az inicializalas soran! Hibakod: ");
    Serial.println(state);
    while (1); // Itt megáll a program, ha hiba van
  }
  
  Serial.println("--- SX1280 Sikeresen inicializalva! Varas az adatokra... ---");

  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);
  radio.setCRC(2); 

  radio.fixedPacketLengthMode(PACKET_LEN);
  radio.setHighSensitivityMode(true);
  radio.setDio1Action(setFlag);

  radio.startReceive();
}

void setup() {
  Serial.begin(921600);
  delay(100);

  // --- OLED ÉS LED INICIALIZÁLÁSA ---
  Wire.begin(SDA_PIN, SCL_PIN);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init hiba");
    while(true);
  }

  display.clearDisplay();
  display.setTextSize(2); 
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,20);
  display.print("Indulas...");
  display.display();

  delay(200);
  strip.begin();
  delay(500);
  strip.setPixelColor(6, 0, 255, 255);
  strip.show();
  delay(500);
  // ------------------------------------

  customSPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  setupRadio();
  
  display.clearDisplay();
  display.setCursor(0, 20);
  display.print("Vetelre kesz");
  display.display();
}

void loop() {
  // Statikus változók a kijelző és LED logikához
  static int lastNumLeds = -1;
  static uint32_t lastColor = 0;
  static bool hasSignal = false;
  static int displayUpdateCount = 0;

  if (receivedFlag) {
    receivedFlag = false;

    int state = radio.readData(videoPacket, PACKET_LEN);
    lastSignalTime = millis(); // Bejövő jel időbélyege (No Signal detektáláshoz)

    bool packetOk = false;
    if (state == RADIOLIB_ERR_NONE && videoPacket[0] == SYNC_BYTE) {
      uint8_t crcCalc = calcCrc8(videoPacket, PACKET_LEN - 1);
      uint8_t crcRecv = videoPacket[PACKET_LEN - 1];
      packetOk = (crcCalc == crcRecv);
    }

    if (!packetOk) {
      crcErrors++; // Ha hibás a csomag (CRC vagy más hiba)
    } else {
      totalPackets++;
      if (totalPackets % RSSI_UPDATE_INTERVAL == 0) {
        // FONTOS: még startReceive() ELŐTT kell lekérni, különben a chip törli a regisztert!
        float rawRssi = radio.getRSSI();
        int rssiInt = (int)rawRssi;
        if (rssiInt > 0)    rssiInt = 0;
        if (rssiInt < -127) rssiInt = -127;
        lastRssi = (int8_t)rssiInt;
      }
    }

    // Csak EZUTÁN indítjuk újra a vételt (A legfontosabb sebesség optimalizáció marad)
    radio.startReceive();

    if (packetOk) {
      // 1. SOROS PORTI KIKÜLDÉS (Azonnal továbbítjuk PC felé, nincs késedelem)
      uint8_t rssiByte = (uint8_t)(-lastRssi);
      uint8_t outBuffer[SERIAL_LEN];
      memcpy(outBuffer, videoPacket, PACKET_LEN);
      outBuffer[PACKET_LEN] = (255 - rssiByte);

      Serial.write(outBuffer, SERIAL_LEN);

      // 2. LED ÉS OLED KIJELZŐ FRISSÍTÉSE (Csak miután az adat már ment a Serial-on)
      int rssi_dbm = (int)lastRssi;

      int numLedsOn = 1;
      uint32_t color = 0;

      int calc_rssi = constrain(rssi_dbm, -91, 0);
      if (calc_rssi <= -62) {
        color = strip.Color(255, 0, 0);
        numLedsOn = map(calc_rssi, -91, -62, 1, 8);
      } 
      else if (calc_rssi <= -32) {
        color = strip.Color(255, 255, 0);
        numLedsOn = map(calc_rssi, -61, -32, 1, 8);
      } 
      else {
        color = strip.Color(0, 255, 0);
        numLedsOn = map(calc_rssi, -31, 0, 1, 8);
      }
      numLedsOn = constrain(numLedsOn, 1, 8);

      if (numLedsOn != lastNumLeds || color != lastColor || !hasSignal) {
        strip.clear();
        for (int i = 0; i < numLedsOn; i++) {
          strip.setPixelColor(i, color);
        }
        strip.show();
        
        lastNumLeds = numLedsOn; 
        lastColor = color;
        hasSignal = true;        
      }

      // --- OLED Kijelző frissítése (Minden 5. jó csomagnál, ahogy kérted) ---
      if (displayUpdateCount >= 5) {
        display.clearDisplay();
        
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print("RSSI:");
        display.print(rssi_dbm);

        display.setTextSize(1);
        display.setCursor(0, 30);
        display.print("Csomagok : ");
        display.print(totalPackets);

        display.setCursor(0, 45);
        display.print("CRC Hiba : ");
        display.print(crcErrors);

        display.display();
        displayUpdateCount = 0;
      }
      displayUpdateCount++;
    }
  } 
  else {
    // --- JEL MEGSZAKADÁS DETEKTÁLÁSA (No Signal) ---
    if (millis() - lastSignalTime >= signalTimeout) {
      if (hasSignal) {
        display.clearDisplay();
        display.setTextSize(2); 
        display.setCursor(0, 20);
        display.print("No Signal!");
        display.display();

        strip.clear();
        for (int i = 0; i < LED_COUNT; i++) {
          strip.setPixelColor(i, 255, 0, 0);
        }
        strip.show();

        hasSignal = false; 
        lastNumLeds = -1;  
        lastColor = 0;
      }
    }
  }
}