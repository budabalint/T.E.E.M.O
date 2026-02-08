#include <Arduino.h>
#include <LoRa_E220.h>


const uint8_t RX_PIN = 6;
const uint8_t TX_PIN = 5;
const uint8_t AUX_PIN = 4;
const uint8_t M1_PIN = 7;
const uint8_t M0_PIN = 8;


const uint8_t MY_ADDH = 0;
const uint8_t MY_ADDL = 2;
const uint8_t CHANNEL = 23; // Ugyanaz a csatorna (433.125 + 23)


LoRa_E220 e220ttl(RX_PIN, TX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600);

void setup() {
  Serial.begin(921600);
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



void loop() {
  // Megvárjuk, amíg összegyűlik 45 bájt (44 adat + 1 RSSI)
  // Így nem blokkolja a loop-ot, ha nincs adat.
  if (e220ttl.available() >= 45) {
    
    // Kiolvas 44 bájtot a 'data'-ba, és a 45.-et az 'rssi'-be
    ResponseStructContainer rc = e220ttl.receiveMessageRSSI(44);

    if (rc.status.code == E220_SUCCESS) {
      // Csak a tiszta adatot küldjük tovább a PC-nek (44 byte)
      // Az RSSI bájt itt az 'rc.rssi'-ben van, de mivel nem írjuk ki, "eltűnik".
      Serial.write((uint8_t*)rc.data, 44);
    }

    // !!! EZ KÖTELEZŐ !!! 
    // Felszabadítja a memóriát. Enélkül betelik a RAM és lefagy az ESP.
    rc.close(); 
  }
}