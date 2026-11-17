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

struct __attribute__((packed)) PacketA_Struct {
  uint8_t startByte; // 0xFE
  uint8_t id;        // 0xAA
  uint8_t seq;
  int16_t roll, pitch, yaw;
  int16_t acc_x, acc_y, acc_z;
  int16_t gyro_x, gyro_y, gyro_z;
  int16_t mag_x, mag_y, mag_z;
  uint16_t tvoc, co2;
  uint16_t voltage1;
  uint32_t current1;
  uint16_t voltage2;
  uint32_t current2;

  uint8_t padding[3]; 
  uint8_t crc;
};

struct __attribute__((packed)) PacketB_Struct {
  uint8_t startByte; // 0xFE
  uint8_t id;        // 0xBB
  uint8_t seq;

  uint16_t temp;
  uint16_t hum;
  int32_t press;
  int32_t lat;
  int32_t lng;
  int32_t speed;
  int32_t alt;
  uint16_t hdop;
  uint8_t sats;
  float white;
  uint32_t lux;

  uint8_t padding[1]; 
  uint8_t crc;
};


void fillRandomPacketA(uint8_t* buffer, uint8_t seq) {

    memset(buffer, 0, TX_PACKET_SIZE);
    

    buffer[0] = 0xFE;
    buffer[1] = 0xAA;
    buffer[2] = seq;


    int offset = 3;
    for(int i=0; i<9; i++) {
        int16_t val = random(-1000, 1000);
        memcpy(buffer + offset, &val, 2); offset += 2;
    }

    for(int i=0; i<3; i++) {
        int16_t val = random(-500, 500);
        memcpy(buffer + offset, &val, 2); offset += 2;
    }

    uint16_t env = random(0, 1000); memcpy(buffer + offset, &env, 2); offset += 2;
    env = random(400, 2000); memcpy(buffer + offset, &env, 2); offset += 2;

    // Power
    uint16_t volt = random(3000, 4200); memcpy(buffer + offset, &volt, 2); offset += 2;
    uint32_t curr = random(100, 5000); memcpy(buffer + offset, &curr, 4); offset += 4;
    volt = random(11000, 12600); memcpy(buffer + offset, &volt, 2); offset += 2;
    curr = random(100, 2000); memcpy(buffer + offset, &curr, 4); offset += 4;

    buffer[TX_PACKET_SIZE - 1] = calculateCRC8(buffer, TX_PACKET_SIZE - 1);
}

void fillRandomPacketB(uint8_t* buffer, uint8_t seq) {
    memset(buffer, 0, TX_PACKET_SIZE);

    buffer[0] = 0xFE;
    buffer[1] = 0xBB;
    buffer[2] = seq;

    int offset = 3;
    

    uint16_t temp = random(2000, 3000); memcpy(buffer + offset, &temp, 2); offset += 2;
    uint16_t hum = random(3000, 6000); memcpy(buffer + offset, &hum, 2); offset += 2;
    int32_t press = random(98000, 103000); memcpy(buffer + offset, &press, 4); offset += 4;
    
    int32_t lat = 470000000 + random(-10000, 10000); memcpy(buffer + offset, &lat, 4); offset += 4;
    int32_t lng = 190000000 + random(-10000, 10000); memcpy(buffer + offset, &lng, 4); offset += 4;
    int32_t spd = random(0, 1000); memcpy(buffer + offset, &spd, 4); offset += 4;
    int32_t alt = random(10000, 50000); memcpy(buffer + offset, &alt, 4); offset += 4;
    uint16_t hdop = random(80, 200); memcpy(buffer + offset, &hdop, 2); offset += 2;
    uint8_t sats = random(4, 12); memcpy(buffer + offset, &sats, 1); offset += 1;

    float white = (float)random(100, 10000); memcpy(buffer + offset, &white, 4); offset += 4;
    uint32_t lux = random(100, 10000); memcpy(buffer + offset, &lux, 4); offset += 4;

    buffer[TX_PACKET_SIZE - 1] = calculateCRC8(buffer, TX_PACKET_SIZE - 1);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("TX Init...");
  display.display();

  strip.begin();
  strip.setBrightness(50);
  strip.show();

  e220ttl.begin();
  
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
    Serial.println("LoRa Config OK");
  } else {
    Serial.println("LoRa Config Error");
    display.println("LoRa Error!");
    display.display();
    while(1);
  }
  c.close();

  display.println("LoRa Ready.");
  display.display();
}

void loop() {
  if (millis() - lastTxTime > 1000) { // csomag küldés 
    lastTxTime = millis();
    sequenceCounter++;

    uint8_t txBuffer[TX_PACKET_SIZE];

    if (sendPacketTypeA) {
        fillRandomPacketA(txBuffer, sequenceCounter);
    } else {
        fillRandomPacketB(txBuffer, sequenceCounter);
    }

    ResponseStatus rs = e220ttl.sendFixedMessage(0xFF, 0xFF, CHANNEL, txBuffer, TX_PACKET_SIZE);
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("TX Sending... ");
    display.print(rs.getResponseDescription());
    
    display.setCursor(0, 20);
    display.printf("Type: %s", sendPacketTypeA ? "Packet A (IMU)" : "Packet B (GPS)");
    
    display.setCursor(0, 40);
    display.printf("Seq: %d", sequenceCounter);
    
    display.setCursor(0, 50);
    display.printf("Size: %d bytes", TX_PACKET_SIZE);
    display.display();

    strip.setPixelColor(0, sendPacketTypeA ? strip.Color(0, 0, 255) : strip.Color(255, 0, 255));
    strip.show();
    delay(100);
    strip.setPixelColor(0, 0);
    strip.show();

    Serial.printf("Sent Packet %c, Seq: %d, CRC: 0x%02X\n", 
                  sendPacketTypeA ? 'A' : 'B', 
                  sequenceCounter, 
                  txBuffer[TX_PACKET_SIZE-1]);
    sendPacketTypeA = !sendPacketTypeA;
  }
}