#include <Arduino.h>
#include <LoRa_E220.h>


const uint8_t RX_PIN = 5;
const uint8_t TX_PIN = 6;
const uint8_t AUX_PIN = 4;
const uint8_t M1_PIN = 7;
const uint8_t M0_PIN = 8;


const uint8_t MY_ADDH = 0;
const uint8_t MY_ADDL = 2;
const uint8_t CHANNEL = 23; // Ugyanaz a csatorna (433.125 + 23)


LoRa_E220 e220ttl(RX_PIN, TX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600);

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

  e220ttl.begin();

  ResponseStructContainer c = e220ttl.getConfiguration();
  
  if (c.status.code == E220_SUCCESS) {
      Configuration configuration = *(Configuration*) c.data;

      configuration.ADDH = MY_ADDH;
      configuration.ADDL = MY_ADDL;
      configuration.CHAN = CHANNEL;


      configuration.SPED.airDataRate = AIR_DATA_RATE_000_24; // 2.4kbps
      configuration.SPED.uartBaudRate = UART_BPS_9600;
      configuration.SPED.uartParity = MODE_00_8N1;
      

      configuration.OPTION.transmissionPower = POWER_10; 
      configuration.OPTION.subPacketSetting = SPS_200_00;
      configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
      configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION; // Fix mód
      configuration.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;

      ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
      Serial.println(rs.getResponseDescription());
      Serial.println("Konfiguráció sikeresen beállítva!");
      Serial.printf("Cím: %d.%d, Csatorna: %d\n", MY_ADDH, MY_ADDL, CHANNEL);
      
  } else {
      Serial.println("Hiba a konfiguráció olvasásakor! Ellenőrizd a bekötést!");
      Serial.println(c.status.getResponseDescription());
  }
  c.close();
  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);

  Serial.println("Vételre kész...");
}

void loop() {
  if (e220ttl.available() > 1) {
    
    ResponseContainer rc = e220ttl.receiveMessage();

    if (rc.status.code != E220_SUCCESS) {
      Serial.print("Hiba a vételnél: ");
      Serial.println(rc.status.getResponseDescription());
    } else {
      Serial.print("PKT [");
      Serial.print(rc.data.length());
      Serial.print(" byte]: ");

      for (int i = 0; i < rc.data.length(); i++) {
        uint8_t byteVal = (uint8_t)rc.data[i];
        if (byteVal < 16) Serial.print("0");
        Serial.print(byteVal, HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
  }
}