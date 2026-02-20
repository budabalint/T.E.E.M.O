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
const uint8_t CHANNEL = 23; // Ugyanaz a csatorna (433.125 + 23)

const uint8_t SDA_PIN = 21;
const uint8_t SCL_PIN = 47;


#define LED_COUNT   8
#define LED_PIN     48

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C



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
  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);


  delay(200);
  strip.begin();
  delay(500);
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
      configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION; // Fix mód
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


// Feltételezve, hogy az Adafruit_NeoPixel könyvtárat használod
// és a 'strip', 'display', 'e220ttl' objektumok inicializálva vannak.

int count = 0;
unsigned long lastSignalTime = 0;
const unsigned long signalTimeout = 1000; // 1 másodperc timeout

// Jelerősség határértékek (A te hardvered/beállításod alapján)
// E220 esetén gyakran az alacsonyabb nyers érték a jobb jel
const int RSSI_STRONG = 40;  // Erős jel (Raw érték)
const int RSSI_WEAK = 120;   // Gyenge jel határa (Raw érték)

void loop() {
  if (e220ttl.available() >= 45) {
    lastSignalTime = millis();
    ResponseStructContainer rc = e220ttl.receiveMessageRSSI(44);

    if (rc.status.code == E220_SUCCESS) {
      int rssi = rc.rssi;
      int calcRssi = rssi;
      
      int strongSignal = 40; 
      int weakSignal = 120;

      if (calcRssi < strongSignal) calcRssi = strongSignal;
      if (calcRssi > weakSignal) calcRssi = weakSignal;

      int numLedsOn = map(calcRssi, weakSignal, strongSignal, 0, LED_COUNT);

      uint32_t color;
      if (numLedsOn <= 2) {
        color = strip.Color(255, 0, 0);
      } else if (numLedsOn <= 5) {
        color = strip.Color(255, 140, 0);
      } else {
        color = strip.Color(0, 255, 0);
      }
      
      strip.clear();
      for (int i = 0; i < numLedsOn; i++) {
        strip.setPixelColor(i, color);
      }
      strip.show();

      if (count >= 5) {
        display.clearDisplay();
        display.setCursor(0, 10);
        display.print(rssi - 255);
        display.display();
        count = 0;
      }
      count++;

    }
    rc.close();
  } else {
    if (millis() - lastSignalTime >= signalTimeout) {
      display.clearDisplay();
      display.setCursor(0, 10);
      display.print("No Signal!");
      display.display();

      strip.clear();
      for (int i = 0; i < LED_COUNT; i++) {
        strip.setPixelColor(i, 255, 0, 0);
      }
      strip.show();
    }
  }
}