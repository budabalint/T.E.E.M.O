#include <CanSat.h>
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <Wire.h>
#include <SGP41.h>
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
    _433radio(RADIO_TX, RADIO_RX, &Serial2, RADIO_AUX, RADIO_433MHZ_M0, RADIO_433MHZ_M1, UART_BPS_RATE_9600),
    _ina3v3(0x4F),
    _ina12v(0x4A)
{
    
};

void CanSat::begin() {
    bus_init(SPI_SPEED, I2C_SPEED, UART_SPEED);
    delay(2000);
    BMEBegin();
    BNOBegin();
    VEMLBegin();
    SGPBegin();
    GPSBegin();
    SDBegin();
    InaBegin();
    RadioSetconfig();
}

void CanSat::InaBegin() {
    _ina3v3.begin();
    if (_ina12v.begin()) {
        Serial.println("INA_12V sikeresen elindult!");
    } else {
        Serial.println("Hiba: INA_12V nem található!");
    }

    if (_ina3v3.begin()) {
        Serial.println("INA_3V3 sikeresen elindult!");
    } else {
        Serial.println("Hiba: INA_3V3 nem található!");
    }
}

void CanSat::BMEBegin() {
    if (_bme.begin()) {
        Serial.println("BME280 sikeresen elindult!");
    } else {
        Serial.println("Hiba: BME280 nem található!");
    }
}

void CanSat::BNOBegin() {
    if (_bno.begin(&SPI)) {
        Serial.println("Sikeres BNO szenzorinicializáció");
        _bno.enableSensors(); 
        digitalWrite(BNO_CS, HIGH);
        
    } else {
        Serial.println("Sikertelen BNO inicializáció");
    }
}

void CanSat::VEMLBegin() {
    if (_veml.begin(&Wire)) {
        Serial.println("Sikeres VEML szenzorinicializáció");
    } else {
        Serial.println("Sikertelen VEML inicializáció");
    }
}

void CanSat::SGPBegin() {
    if (_sgp.begin()) {
        Serial.println("Sikeres SGP30 szenzorinicializáció");
    } else {
        Serial.println("Sikertelen SGP30 inicializáció");
    }
}

void CanSat::GPSBegin() {
    if (_gps.begin(GPS_UART_SPEED)) {
        Serial.println("Sikeres GPS szenzorinicializáció");
    } else {
        Serial.println("Sikertelen GPS inicializáció");
    }
}

void CanSat::SDBegin() {
    if (!_sd.begin(SdSpiConfig(SD_CARD_CS, SHARED_SPI, SPI_FREQ, &SPI))) {
        Serial.println("sd init failled");
    } else {
        Serial.println("sd init sucess");
    }
    if (!_file.open("data.bin", O_RDWR | O_CREAT | O_APPEND)) {
        Serial.println("file open failled");
    } else {
        Serial.println("sd init sucess");
    }
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

        configuration.SPED.airDataRate = AIR_DATA_RATE_111_625;
        configuration.SPED.uartBaudRate = UART_BPS_115200;
        configuration.SPED.uartParity = MODE_00_8N1;

        configuration.OPTION.transmissionPower = POWER_10;
        configuration.OPTION.subPacketSetting = SPS_200_00;
        configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;

        configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
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
    Serial2.begin(115200);
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



void CanSat::I2CScan() {
    byte error, address;
    int nDevices;
    
    Serial.println("Scanning...");
    
    nDevices = 0;
    for(address = 1; address < 127; address++ )
    {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
    
        if (error == 0)
        {
        Serial.print("I2C device found at address 0x");
        if (address<16)
            Serial.print("0");
        Serial.print(address,HEX);
        Serial.println("  !");
    
        nDevices++;
        }
        else if (error==4)
        {
        Serial.print("Unknown error at address 0x");
        if (address<16)
            Serial.print("0");
        Serial.println(address,HEX);
        }    
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
    else
        Serial.println("done\n");
}


void CanSat::bus_init(int spi_speed, int i2c_speed, int serial_speed) {
    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    SPI.setFrequency(spi_speed);
    Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
    Wire.setClock(i2c_speed);
    Serial.begin(serial_speed);
}