#include <Arduino.h>
#include <LoRa_E220.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const uint8_t RX_PIN = 5;
const uint8_t TX_PIN = 6;
const uint8_t AUX_PIN = 4;
const uint8_t M1_PIN = 7;
const uint8_t M0_PIN = 8;

const uint8_t MY_ADDH = 0;
const uint8_t MY_ADDL = 2;
const uint8_t CHANNEL = 67;

const uint8_t SDA_PIN = 21;
const uint8_t SCL_PIN = 47;

#define LED_COUNT   8
#define LED_PIN     48

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

// --- Statisztikai változók ---
unsigned long totalPackets = 0;
unsigned long crcErrors = 0;

// CRC-8 kalkulátor függvény
uint8_t calculateCRC8(const uint8_t *data, size_t len) {
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

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
LoRa_E220 e220ttl(RX_PIN, TX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(921600);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  digitalWrite(M0_PIN, HIGH);
  digitalWrite(M1_PIN, HIGH);

  delay(1000);

  e220ttl.begin();

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
  strip.setPixelColor(6, 0,255,255);
  strip.show();
  delay(500);
  
  while(Serial2.available()) {
    Serial2.read();
  }

  e220ttl.begin();

  ResponseStructContainer c = e220ttl.getConfiguration();

  if (c.status.code == E220_SUCCESS) {
    Configuration configuration = *(Configuration*) c.data;

    configuration.ADDH = MY_ADDH;
    configuration.ADDL = MY_ADDL;
    configuration.CHAN = CHANNEL;

    configuration.SPED.airDataRate = AIR_DATA_RATE_111_625;
    configuration.SPED.uartBaudRate = UART_BPS_115200;
    configuration.SPED.uartParity = MODE_00_8N1;
    
    configuration.OPTION.transmissionPower = POWER_10; 
    configuration.OPTION.subPacketSetting = SPS_200_00;
    configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
    configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
    configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;

    ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
    Serial.println(rs.getResponseDescription());
    Serial.println("Konfiguráció sikeresen beállítva!");
    Serial.printf("Cím: %d.%d, Csatorna: %d\n", MY_ADDH, MY_ADDL, CHANNEL);

  } else {
    Serial.println("Hiba a konfiguráció olvasásakor! Ellenőrizd a bekötést!");
    Serial.println(c.status.getResponseDescription());
  }
  c.close();
  e220ttl.setMode(MODE_0_NORMAL);

  Serial2.begin(115200);
  Serial.println("Vételre kész...");
}

unsigned long lastSignalTime = 0;
const unsigned long signalTimeout = 1000;

void loop() {
  static int lastNumLeds = -1;
  static uint32_t lastColor = 0;
  static bool hasSignal = false;
  static int displayUpdateCount = 0;

  // Ha megjött a 44 bájtos adat + 1 bájt RSSI = 45 bájt
  if (e220ttl.available() >= 45) {
    lastSignalTime = millis();
    ResponseStructContainer rc = e220ttl.receiveMessageRSSI(44);

    if (rc.status.code == E220_SUCCESS) {
      totalPackets++;

      // A nyers memória bájttömbként kezelése
      uint8_t *buffer = (uint8_t*) rc.data; 
      
      uint8_t receivedCRC = buffer[43]; 
      uint8_t calculatedCRC = calculateCRC8(buffer, 43);

      if(receivedCRC != calculatedCRC) {
        crcErrors++; 
      }

      // RSSI kiszámítása dbm-ben
      int rssi_dbm = rc.rssi - 255; 

      // ---------------------------------------------------------
      // ÚJ RÉSZ: NYERS BINÁRIS ADAT KIKÜLDÉSE A SOROS PORTON
      // ---------------------------------------------------------
      // Létrehozunk egy 45 bájtos tömböt (44 adat + 1 RSSI)
      uint8_t outBuffer[45];
      
      // Bemásoljuk a 44 bájtnyi nyers csomagot
      memcpy(outBuffer, buffer, 44);
      
      // Az RSSI érték hozzáfűzése a 45. (utolsó) helyre (index 44)
      // Az előző kódod logikáját követve: (255 - abszolút_rssi)
      uint8_t rssiByte = (uint8_t)(-rssi_dbm);
      outBuffer[44] = (255 - rssiByte);
      
      // Kiküldjük a PC/Földi állomás felé
      Serial.write(outBuffer, 45);
      // ---------------------------------------------------------

      // --- LED és OLED kijelző logika (Változatlan) ---
      if (rssi_dbm >= -130) { 
        
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

        // --- OLED Kijelző frissítése ---
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
    rc.close();

  } else {
    // --- Jel megszakadás detektálása ---
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