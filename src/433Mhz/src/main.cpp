#include <LoRa_E220.h>
#include <Arduino.h>

const uint8_t RADIO_TX = 6;
const uint8_t RADIO_RX = 5;
const uint8_t RADIO_M1 = 7;
const uint8_t RADIO_M0 = 8;
const uint8_t RADIO_AUX = 4;

const uint8_t DESTINATION_ADDH = 0;
const uint8_t DESTINATION_ADDL = 2;
const uint8_t DESTINATION_CHAN = 70;


LoRa_E220 e220ttl(&Serial2, RADIO_AUX, RADIO_M0, RADIO_M1);

struct MessageHumidity {
  char type[5];
  char message[8];
  byte humidity;
};

void setup() {
  
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);

  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RADIO_RX, RADIO_TX); 
  delay(1000);

  e220ttl.begin();
  delay(500);

  ResponseStructContainer c = e220ttl.getConfiguration();
  
  if (c.status.code == E220_SUCCESS) {
      Configuration configuration = *(Configuration*) c.data;

      configuration.ADDH = 0;
      configuration.ADDL = 1;
      
      configuration.SPED.airDataRate = AIR_DATA_RATE_111_625;
      configuration.SPED.uartBaudRate = UART_BPS_115200;
      configuration.SPED.uartParity = MODE_00_8N1;
      configuration.OPTION.transmissionPower = POWER_10;
      
      configuration.CHAN = DESTINATION_CHAN;
      
      configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
      configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;

      ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_LOSE);
      Serial.println(rs.getResponseDescription());
  } else {
      Serial.print("Kritikus hiba a konfiguracio olvasasakor: ");
      Serial.println(c.status.getResponseDescription());
  }
  
  c.close();
}

void loop() {
  MessageHumidity humiMessage = { "HUMI", "Room101", 80};

  ResponseStatus rsH = e220ttl.sendFixedMessage(
    DESTINATION_ADDH, 
    DESTINATION_ADDL,
    DESTINATION_CHAN,
    &humiMessage,
    sizeof(humiMessage)
  );

  Serial.println(rsH.getResponseDescription());
  
  delay(2000); 
}