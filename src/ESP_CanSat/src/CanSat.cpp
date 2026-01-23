#include <CanSat.h>
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <Wire.h>
#include <SGP30.h>
#include <Create_Packet.h>
#include <config.h>


#define SPI_FREQ SD_SCK_MHZ(8)

CanSat::CanSat():
    _bno(BNO_INT, BNO_CS, BNO_RST),
    _bme(),
    _veml(),
    _sgp(),
    _gps(),
    _sd(),
    _file(),
    _433radio(RADIO_TX, RADIO_RX, &Serial2, RADIO_AUX, RADIO_433MHZ_M0, RADIO_433MHZ_M1, UART_BPS_RATE_9600)
{
    
};

void CanSat::begin() {
    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    SPI.setFrequency(8000000);
    //Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
    //Wire.setClock(1000000);
    Serial.begin(4000000);

    if (_bme.begin()) {
        Serial.println("BME280 sikeresen elindult!");
    } else {
        Serial.println("Hiba: BME280 nem található!");
    }
    
    if (_bno.begin(&SPI)) {
        Serial.println("Sikeres BNO szenzorinicializáció");
        _bno.enableSensors(); 
        digitalWrite(BNO_CS, HIGH);
        
    } else {
        Serial.println("Sikertelen BNO inicializáció");
    }

    if (_veml.begin(&Wire)) {
        Serial.println("Sikeres VEML szenzorinicializáció");
    } else {
        Serial.println("Sikertelen VEML inicializáció");
    }

    if (_sgp.begin()) {
        Serial.println("Sikeres SGP30 szenzorinicializáció");
    } else {
        Serial.println("Sikertelen SGP30 inicializáció");
    }

    if (_gps.begin(9600)) {
        Serial.println("Sikeres GPS szenzorinicializáció");
    } else {
        Serial.println("Sikertelen GPS inicializáció");
    }

    if (!_sd.begin(SdSpiConfig(SD_CARD_CS, SHARED_SPI, SPI_FREQ, &SPI))) {
        Serial.println("sd init failled");
    } else {
        Serial.println("sd init sucess");
    }

    if (!_file.open("data.bin", O_RDWR | O_CREAT | O_TRUNC)) {
        Serial.println("file open failled");
    } else {
        Serial.println("sd init sucess");
    }
    canSat.RadioSetconfig();


}

void CanSat::RadioSetconfig() {
    delay(1000);

    pinMode(RADIO_433MHZ_M1, OUTPUT);
    pinMode(RADIO_433MHZ_M0, OUTPUT);
    digitalWrite(RADIO_433MHZ_M1, HIGH);
    digitalWrite(RADIO_433MHZ_M0, HIGH);
    delay(1000);

    _433radio.begin();
    delay(200);

    while(Serial2.available()) {
        Serial2.read();
    }

    ResponseStructContainer c = _433radio.getConfiguration();

    if (c.status.code == E220_SUCCESS) {
        Configuration configuration = *(Configuration*) c.data;

        configuration.ADDH = SRC_ADDH;
        configuration.ADDL = SRC_ADDL;
        configuration.CHAN = CHANNEL;

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

        ResponseStatus rs = _433radio.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
        Serial.println(rs.getResponseDescription());
    } else {
        Serial.println(c.status.getResponseDescription());
    }
    c.close();

    _433radio.setMode(MODE_0_NORMAL);
}


void CanSat::sendRadioMsg(uint8_t addh, uint8_t addl, uint8_t chan, const void *msg, const uint8_t size) {
    ResponseStatus rs = _433radio.sendFixedMessage(addh, addl, chan, msg, size);
    
    if (rs.code != E220_SUCCESS) {
        Serial.print("Send Error: ");
        Serial.println(rs.getResponseDescription());
    } else {
        Serial.println(rs.getResponseDescription());
    }
}