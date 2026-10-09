#include <Arduino.h>
#include <LoRa_E220.h>

const uint8_t ESP_RX_PIN = 5;
const uint8_t ESP_TX_PIN = 6;
const uint8_t AUX_PIN = 4;
const uint8_t M1_PIN = 7;
const uint8_t M0_PIN = 8;

const uint8_t SRC_ADDH = 0;
const uint8_t SRC_ADDL = 1;
const uint8_t DEST_ADDH = 0;
const uint8_t DEST_ADDL = 2;
const uint8_t CHANNEL_433_125 = 23;

LoRa_E220 e220ttl(ESP_RX_PIN, ESP_TX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600);

struct Message {
  char type[5];
  unsigned long count;
  float val;
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  digitalWrite(M0_PIN, HIGH);
  digitalWrite(M1_PIN, HIGH);
  delay(1000);

  e220ttl.begin();
  delay(200);

  while(Serial2.available()) {
    Serial2.read();
  }

  ResponseStructContainer c = e220ttl.getConfiguration();

  if (c.status.code == E220_SUCCESS) {
      Configuration configuration = *(Configuration*) c.data;

      configuration.ADDH = SRC_ADDH;
      configuration.ADDL = SRC_ADDL;
      configuration.CHAN = CHANNEL_433_125;

      configuration.SPED.airDataRate = AIR_DATA_RATE_000_24;
      configuration.SPED.uartBaudRate = UART_BPS_9600;
      configuration.SPED.uartParity = MODE_00_8N1;

      configuration.OPTION.transmissionPower = POWER_10;
      configuration.OPTION.subPacketSetting = SPS_200_00;
      configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;

      configuration.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;
      configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
      configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
      configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;

      ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
      Serial.println(rs.getResponseDescription());
  } else {
      Serial.println(c.status.getResponseDescription());
  }
  c.close();

  e220ttl.setMode(MODE_0_NORMAL);
  delay(1000);
  while(Serial2.available()) { Serial2.read(); }
}

void loop() {
  static unsigned long counter = 0;
  struct Message msg = { "DATA", counter++, 21.5 };

  ResponseStatus rs = e220ttl.sendFixedMessage(
    DEST_ADDH,
    DEST_ADDL,
    CHANNEL_433_125,
    &msg,
    sizeof(Message)
  );

  Serial.println(rs.getResponseDescription());
  delay(200);
}