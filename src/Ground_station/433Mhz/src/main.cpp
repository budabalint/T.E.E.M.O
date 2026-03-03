#include <Arduino.h>
#include <LoRa_E220.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <settings.h>

#define TX_PACKET_SIZE 44 

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
LoRa_E220 e220ttl(RX_PIN, TX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastTxTime = 0;
uint8_t sequenceCounter = 0;
bool sendPacketTypeA = true;

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

void sendRandomPacket() {
  uint8_t txBuffer[TX_PACKET_SIZE];
  memset(txBuffer, 0, TX_PACKET_SIZE);

  txBuffer[0] = 0xFE;
  txBuffer[1] = sendPacketTypeA ? 0xAA : 0xBB;
  txBuffer[2] = sequenceCounter;

  int offset = 3;

  if (sendPacketTypeA) {
    for(int i = 0; i < 12; i++) {
        int16_t val = random(-1000, 1000);
        memcpy(txBuffer + offset, &val, 2); 
        offset += 2;
    }

    uint16_t tvoc = random(0, 500); memcpy(txBuffer + offset, &tvoc, 2); offset += 2;
    uint16_t co2 = random(400, 1200); memcpy(txBuffer + offset, &co2, 2); offset += 2;

    uint16_t v1 = random(3200, 4200); memcpy(txBuffer + offset, &v1, 2); offset += 2; // Voltage1
    uint32_t c1 = random(100, 2000);  memcpy(txBuffer + offset, &c1, 4); offset += 4; // Current1
    uint16_t v2 = random(11000, 12600); memcpy(txBuffer + offset, &v2, 2); offset += 2; // Voltage2
    uint32_t c2 = random(500, 3000);  memcpy(txBuffer + offset, &c2, 4); offset += 4; // Current2

  } else {

    uint16_t temp = random(2000, 3500); memcpy(txBuffer + offset, &temp, 2); offset += 2; // 20.00 - 35.00 C
    uint16_t hum = random(3000, 6000);  memcpy(txBuffer + offset, &hum, 2); offset += 2; // 30.00 - 60.00 %
    int32_t press = random(9800000, 10200000); memcpy(txBuffer + offset, &press, 4); offset += 4;

    int32_t lat = 474979120 + random(-1000, 1000); memcpy(txBuffer + offset, &lat, 4); offset += 4;
    int32_t lng = 190402350 + random(-1000, 1000); memcpy(txBuffer + offset, &lng, 4); offset += 4;
    int32_t spd = random(0, 1500); memcpy(txBuffer + offset, &spd, 4); offset += 4;
    int32_t alt = random(10000, 50000); memcpy(txBuffer + offset, &alt, 4); offset += 4;
    uint16_t hdop = random(80, 200); memcpy(txBuffer + offset, &hdop, 2); offset += 2;
    uint8_t sats = random(5, 12); memcpy(txBuffer + offset, &sats, 1); offset += 1;

    float white = (float)random(100, 5000); memcpy(txBuffer + offset, &white, 4); offset += 4;
    uint32_t lux = random(100, 5000); memcpy(txBuffer + offset, &lux, 4); offset += 4;
  }

  txBuffer[TX_PACKET_SIZE - 1] = calculateCRC8(txBuffer, TX_PACKET_SIZE - 1);

  ResponseStatus rs = e220ttl.sendFixedMessage(0xFF, 0xFF, CHANNEL, txBuffer, TX_PACKET_SIZE);
  
  Serial.printf("Sent Packet %s Seq: %d -> %s\n", 
    sendPacketTypeA ? "A (IMU)" : "B (GPS)", 
    sequenceCounter, 
    rs.getResponseDescription().c_str());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("--- TRANSMITTER ---");
  display.setCursor(0, 20);
  display.printf("Sending: %s", sendPacketTypeA ? "PKT A" : "PKT B");
  display.setCursor(0, 35);
  display.printf("Seq: %d", sequenceCounter);
  display.setCursor(0, 50);
  display.printf("Status: %s", rs.getResponseDescription().c_str());
  display.display();


  strip.setPixelColor(0, sendPacketTypeA ? strip.Color(0, 0, 255) : strip.Color(0, 255, 0));
  strip.show();
  delay(100);
  strip.setPixelColor(0, 0);
  strip.show();

  sequenceCounter++;
  sendPacketTypeA = !sendPacketTypeA;
}

void setup() {

  Serial.begin(921600); 
  delay(500);
  Wire.begin(SDA_PIN, SCL_PIN);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  digitalWrite(M0_PIN, HIGH);
  digitalWrite(M1_PIN, HIGH);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    for(;;); 
  }
  display.clearDisplay();
  display.setTextSize(2); 
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,20);
  display.print("Init Radio...");
  display.display();

  strip.begin();
  strip.show(); 
  delay(500);

  e220ttl.begin();
  delay(200);
  while(Serial2.available()) Serial2.read();

  ResponseStructContainer c = e220ttl.getConfiguration();
  if (c.status.code == E220_SUCCESS) {
    Configuration configuration = *(Configuration*) c.data;
    configuration.ADDH = MY_ADDH;
    configuration.ADDL = MY_ADDL;
    configuration.CHAN = CHANNEL;
    configuration.SPED.airDataRate = AIR_DATA_RATE_000_24; 
    configuration.SPED.uartBaudRate = UART_BPS_115200;
    configuration.SPED.uartParity = MODE_00_8N1;
    configuration.OPTION.transmissionPower = POWER_10; 
    configuration.OPTION.subPacketSetting = SPS_200_00;
    
    configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;
    
    configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
    configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
    e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  }
  c.close();
  
  e220ttl.setMode(MODE_0_NORMAL);
  Serial2.begin(115200); 

  display.clearDisplay();
  display.setCursor(0,20);
  display.print("TX MODE!");
  display.display();
  delay(1000);
}

void loop() {
  if (millis() - lastTxTime > 400) { // az adás gyakoriságát állítod 
    lastTxTime = millis();
    sendRandomPacket();
  }
}