#include <CanSat.h>
#include <Arduino.h>
#include <BNO085.h>
#include <hardware_pins.h>
#include <SPI.h>
#include <Wire.h>
#include <SGP41.h>
#include <Create_Packet.h>
#include <config.h>
#include "SX1280.h"



#define SPI_FREQ SD_SCK_MHZ(8)
extern SemaphoreHandle_t dataMutex; 
extern SemaphoreHandle_t spiMutex; 
CanSat::CanSat():
    _bno(),
    _bme(),
    _veml(),
    _sgp(),
    _gps(),
    _sd(),
    _file(),
    _433radio(RADIO_TX, RADIO_RX, &Serial2, RADIO_AUX, RADIO_433MHZ_M0, RADIO_433MHZ_M1, UART_BPS_RATE_9600),
    _ina3v3(0x4F),
    _ina12v(0x4A),
    _camera(),
    _videoRadio(SX1280_NSS, SX1280_DIO1, SX1280_NRST, SX1280_BUSY, &SPI)
{
    
};

void CanSat::begin() {
    SetPinModes();
    bus_init(SPI_SPEED, I2C_SPEED, UART_SPEED);
    delay(5000);
    BMEBegin();
    BNOBegin();
    VEMLBegin();
    SGPBegin();
    GPSBegin();
    SDBegin();
    InaBegin();
    //CameraBegin();
    LoraRadioSetconfig();
    RF24RadioSetconfig();
}

void CanSat::InaBegin() {
    if (_ina12v.begin(500, 0.150)) {
        Serial.println("INA_12V sikeresen elindult!");
        ina12v_ok = true;
    } else {
        Serial.println("Hiba: INA_12V nem található!");
        ina12v_ok = false;
    }

    if (_ina3v3.begin(0.5, 0.082)) {
        Serial.println("INA_3V3 sikeresen elindult!");
        ina3v3_ok = true;
    } else {
        Serial.println("Hiba: INA_3V3 nem található!");
        ina3v3_ok = false;
    }
}

void CanSat::BMEBegin() {
    if (_bme.begin()) {
        Serial.println("BME280 sikeresen elindult!");
        bme_ok = true;
    } else {
        Serial.println("Hiba: BME280 nem található!");
        bme_ok = false;
    }
}

void CanSat::BNOBegin() {
    if (_bno.begin()) {
        Serial.println("Sikeres BNO szenzorinicializáció");
        _bno.enableSensors(); 
        bno_ok = true;
        
    } else {
        Serial.println("Sikertelen BNO inicializáció");
        bno_ok = false;
    }
}

void CanSat::VEMLBegin() {
    if (_veml.begin(&Wire)) {
        Serial.println("Sikeres VEML szenzorinicializáció");
        veml_ok = true;
    } else {
        Serial.println("Sikertelen VEML inicializáció");
        veml_ok = false;
    }
}

void CanSat::SGPBegin() {
    if (_sgp.begin()) {
        Serial.println("Sikeres SGP30 szenzorinicializáció");
        sgp_ok = true;
    } else {
        Serial.println("Sikertelen SGP30 inicializáció");
        sgp_ok = false;
    }
}

void CanSat::GPSBegin() {
    if (_gps.begin(GPS_UART_SPEED)) {
        Serial.println("Sikeres GPS szenzorinicializáció");
        gps_ok = true;
    } else {
        Serial.println("Sikertelen GPS inicializáció");
        gps_ok = false;
    }
}

void CanSat::SDBegin() {
    if (!_sd.begin(SdSpiConfig(SD_CARD_CS, SHARED_SPI, SD_SPI_SPEED, &SPI))) {
        Serial.println("sd init failled");
    } else {
        Serial.println("sd init sucess");
    }
    if (!_file.open(SD_FILENAME, O_RDWR | O_CREAT | O_APPEND)) {
        Serial.println("file open failled");
    } else {
        Serial.println("sd file open sucess");
    }
}



void CanSat::CameraBegin() {
    if (_camera.begin()) {
        Serial.println("Sikeres kamera init.");
    } else {
        Serial.println("Sikertelen kamera init.");
    }
}




void CanSat::LoraRadioSetconfig() {
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
        configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;

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
    /*    if (rs.code != E220_SUCCESS) {
        Serial.print("Send Error: ");
        Serial.println(rs.getResponseDescription());
    } else {
        Serial.println(rs.getResponseDescription());
    }*/
}


void CanSat::sendRawDataSX1280(const uint8_t* data, size_t length) {
    if (rf24_ok) {
        _videoRadio.transmitRawPadded(data, length);
    }
}

void CanSat::SetPinModes() {
    pinMode(BNO_CS, OUTPUT);
    pinMode(SX1280_NRST, OUTPUT);
    pinMode(SD_CARD_CS, OUTPUT);
    digitalWrite(SD_CARD_CS, HIGH);
    pinMode(CAM_CS, OUTPUT);
    digitalWrite(CAM_CS, HIGH);

    //pinMode(BNO_INT, INPUT);
    //pinMode(CAM_INT, INPUT);
    //pinMode(RADIO_24GHZ_INT, INPUT);

    pinMode(CAM_RST, OUTPUT);
    pinMode(SX1280_NRST, OUTPUT);
    pinMode(BNO_RST, OUTPUT);
}

void CanSat::RF24RadioSetconfig() {
    Serial.println("SX1280 (2.4GHz) inicializalas a kozos SPI buszon...");
    
    // A bus_init már elindította az SPI-t, csak a rádiót élesztjük
    if (_videoRadio.begin()) {
        Serial.println("SX1280 sikeresen elindult!");
        rf24_ok = true;
    } else {
        Serial.println("Hiba: SX1280 nem talalhato / hibas init!");
        rf24_ok = false;
    }
}

// 4. Wrapper metódus a videó streameléshez
// (Így néz ki kijavítva)
void CanSat::streamVideo(const char* path, Stream &out, void (*idleCb)()) {
    if (rf24_ok) {
        _videoRadio.streamMjpegFromFS(path, out, idleCb);
    } else {
        Serial.println("SX1280 hiba: video nem kuldheto!");
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
    //pinMode(SENSOR_I2C_SDA, INPUT_PULLUP);
    //pinMode(SENSOR_I2C_SCL, INPUT_PULLUP);
    pinMode(BNO_CS, OUTPUT);
    digitalWrite(BNO_CS, HIGH);
    SPI.begin(Sensor_SPI_SCL, Sensor_SPI_MISO, Sensor_SPI_MOSI);
    SPI.setFrequency(spi_speed);
    Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
    Wire.setClock(i2c_speed);
    Serial.begin(serial_speed);
}


enum FlightState {
    STATE_PRE_FLIGHT,
    STATE_IN_FLIGHT,
    STATE_LANDED
};

volatile FlightState currentFlightState = STATE_PRE_FLIGHT;


float basePressure = 0.0;
float baseGpsAlt = 0.0;
bool hasInitialGpsFix = false;

// --- INDÍTÁS-DETEKTÁLÁS: GÖRDÜLŐ ELŐZMÉNY BUFFER ---
// Ahelyett, hogy a kezdeti (bázis) értékhez hasonlítanánk, egy kb. 6 másodperces
// history buffert tartunk, és az 5 másodperccel korábbi mintához hasonlítunk.
// Ez könnyebben tesztelhetővé teszi a logikát (nem kell a teljes repülést szimulálni,
// elég pl. felkapni/lengetni a CanSat-ot).
#define ALT_HISTORY_SIZE 60 // 60 minta @ kb. 100ms/minta = 6 másodperc puffer

float pressHistory[ALT_HISTORY_SIZE];
uint32_t pressHistoryTime[ALT_HISTORY_SIZE];
int pressHistoryIdx = 0;
bool pressHistoryFull = false;

float gpsAltHistory[ALT_HISTORY_SIZE];
uint32_t gpsAltHistoryTime[ALT_HISTORY_SIZE];
int gpsAltHistoryIdx = 0;
bool gpsAltHistoryFull = false;

void pushHistory(float value, uint32_t now, float *histArr, uint32_t *timeArr, int &idx, bool &full) {
    histArr[idx] = value;
    timeArr[idx] = now;
    idx = (idx + 1) % ALT_HISTORY_SIZE;
    if (idx == 0) full = true;
}

// Megkeresi a legrégebbi mintát, ami legalább msAgo-val korábbi a "now"-nál.
// Visszaadja true-val, ha talált ilyet (van elég history), false-szal ha még nincs elég adat.
bool getValueFromPast(uint32_t msAgo, uint32_t now, float *histArr, uint32_t *timeArr, int idx, bool full, float &outValue) {
    int count = full ? ALT_HISTORY_SIZE : idx;
    if (count == 0) return false;

    int searchIdx = (idx - 1 + ALT_HISTORY_SIZE) % ALT_HISTORY_SIZE;
    for (int i = 0; i < count; i++) {
        if (now - timeArr[searchIdx] >= msAgo) {
            outValue = histArr[searchIdx];
            return true;
        }
        searchIdx = (searchIdx - 1 + ALT_HISTORY_SIZE) % ALT_HISTORY_SIZE;
    }
    return false; // még nincs elég régi minta a bufferben
}