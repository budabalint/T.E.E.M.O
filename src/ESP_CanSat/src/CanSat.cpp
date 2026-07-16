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
    _ina3v3.begin();
    if (_ina12v.begin()) {
        Serial.println("INA_12V sikeresen elindult!");
        ina12v_ok = true;
    } else {
        Serial.println("Hiba: INA_12V nem található!");
        ina12v_ok = false;
    }

    if (_ina3v3.begin()) {
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

void TaskFlightController(void *pvParameters) {
    pinMode(CAM_POWER_PIN, OUTPUT);
    digitalWrite(CAM_POWER_PIN, LOW); // Kamera alapból kikapcsolva

    vTaskDelay(pdMS_TO_TICKS(5000)); // Várjunk, amíg a szenzorok stabilizálódnak indulás után

    // --- 1. KALIBRÁCIÓ (Alapnyomás és magasság lekérése mutextel védve) ---
    float tempPressSum = 0;
    int validBmeSamples = 0;

    for (int i = 0; i < 10; i++) {
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (canSat.bme_ok) {
                tempPressSum += canSat._bme.readPressure();
                validBmeSamples++;
            }
            xSemaphoreGive(dataMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (validBmeSamples > 0) {
        basePressure = tempPressSum / validBmeSamples;
    }

    // Kezdeti GPS fix ellenőrzése
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (canSat.gps_ok && canSat._gps.getSatellites() >= 4) {
            baseGpsAlt = canSat._gps.getAltitude();
            hasInitialGpsFix = true;
        }
        xSemaphoreGive(dataMutex);
    }

    // --- LANDOLÁSI STABILITÁS SZÁMLÁLÓK ---
    uint32_t bmeStableStartTime = 0;
    uint32_t bnoStableStartTime = 0;
    uint32_t gpsStableStartTime = 0;

    // --- INDÍTÁSI GYORSULÁS SZÁMLÁLÓ (folyamatos 2g feletti idő méréséhez) ---
    uint32_t bnoAccStartTime = 0;

    float lastPressure = basePressure;
    float lastGpsAlt = baseGpsAlt;

    const float GPS_ALT_TOLERANCE = 2.0f; // 2 méter ugrálás megengedett
    const uint32_t REQUIRED_STABLE_TIME = 10000; // 10 másodperc (10000 ms) kell a landoláshoz

    // --- INDÍTÁS-DETEKTÁLÁS PARAMÉTEREI ---
    const uint32_t LAUNCH_WINDOW_MS = 5000;       // 5 másodperces ablak a magasságváltozás nézéséhez
    const float LAUNCH_ALT_CHANGE_M = 7.0f;       // 7 méteres változás kell az ablakon belül
    const float LAUNCH_ACC_G_THRESHOLD = 2.0f * 9.81f; // 2g gyorsulás (m/s^2-ben)
    const uint32_t LAUNCH_ACC_STABLE_TIME = 300;  // 0.3 másodpercig kell folyamatosan fennállnia

    while (1) {
        float currentPress = 0;
        float currentGpsAlt = 0;
        uint8_t currentSats = 0;
        Vector3 acc = {0, 0, 0};
        Vector3 gyro = {0, 0, 0};

        // --- SZENZOROK KIOLVASÁSA (Mutex-szel védve, nehogy a Packet taszk belezavarjon) ---
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (canSat.bme_ok) {
                currentPress = canSat._bme.readPressure();
            }
            if (canSat.bno_ok) {
                canSat._bno.update();
                acc = canSat._bno.getLinearAcceleration();
                gyro = canSat._bno.getGyroscope();
            }
            if (canSat.gps_ok) {
                canSat._gps.encode(); 
                currentGpsAlt = canSat._gps.getAltitude();
                currentSats = canSat._gps.getSatellites();
            }
            xSemaphoreGive(dataMutex);
        }

        uint32_t now = millis();

        // --- ELŐZMÉNY BUFFER FRISSÍTÉSE (mindig, állapottól függetlenül) ---
        if (canSat.bme_ok) {
            pushHistory(currentPress, now, pressHistory, pressHistoryTime, pressHistoryIdx, pressHistoryFull);
        }
        if (canSat.gps_ok) {
            pushHistory(currentGpsAlt, now, gpsAltHistory, gpsAltHistoryTime, gpsAltHistoryIdx, gpsAltHistoryFull);
        }

        // --- AUTOMATIKUS BME NYOMÁS MÉRTÉKEGYSÉG DETEKTÁLÁS (Pa vagy hPa) ---
        bool isPa = (basePressure > 10000.0f); 
        float pressTolerance = isPa ? 50.0f : 0.5f;   // Zajos ugrálás kiküszöbölése (kb 4-5 méter)
        float pressDropPerMeter = isPa ? 12.0f : 0.12f; // kb. 1 méter magasságváltozásnak megfelelő nyomásesés

        // --- BNO VEKTOROK MAGNITÚDÓJÁNAK (ÖSSZESÍTÉSÉNEK) KISZÁMÍTÁSA ---
        // A Linear Acceleration a gravitációt kivonja! Tehát nyugalomban kb 0 m/s^2.
        float accMag = sqrt(pow(acc.x, 2) + pow(acc.y, 2) + pow(acc.z, 2));
        float gyroMag = sqrt(pow(gyro.x, 2) + pow(gyro.y, 2) + pow(gyro.z, 2));

        // --- SZAVAZATOK KIÉRTÉKELÉSE ---
        bool bmeVote = false;
        bool bnoVote = false;
        bool gpsVote = false;

        if (currentFlightState == STATE_PRE_FLIGHT) {
            // 1. Légnyomás szavazat: az 5 másodperccel ezelőtti mintához képest
            //    legalább LAUNCH_ALT_CHANGE_M (7m) magasságnövekedésnek megfelelő nyomásesés kell
            float pastPress;
            if (getValueFromPast(LAUNCH_WINDOW_MS, now, pressHistory, pressHistoryTime, pressHistoryIdx, pressHistoryFull, pastPress)) {
                float pressDrop = pastPress - currentPress; // nyomásesés = magasság nő
                if (pressDrop > (LAUNCH_ALT_CHANGE_M * pressDropPerMeter)) bmeVote = true;
            }

            // 2. Gyorsulás szavazat: folyamatosan 2g felett kell lennie legalább 0.3 másodpercig
            if (accMag > LAUNCH_ACC_G_THRESHOLD) {
                if (bnoAccStartTime == 0) bnoAccStartTime = now;
                if (now - bnoAccStartTime >= LAUNCH_ACC_STABLE_TIME) bnoVote = true;
            } else {
                bnoAccStartTime = 0;
            }

            // 3. GPS szavazat: az 5 másodperccel ezelőtti mintához képest
            //    legalább LAUNCH_ALT_CHANGE_M (7m) magasságnövekedés kell
            float pastGpsAlt;
            if (currentSats >= 4 && getValueFromPast(LAUNCH_WINDOW_MS, now, gpsAltHistory, gpsAltHistoryTime, gpsAltHistoryIdx, gpsAltHistoryFull, pastGpsAlt)) {
                if ((currentGpsAlt - pastGpsAlt) > LAUNCH_ALT_CHANGE_M) gpsVote = true;
            }

            // Döntés (3-ból 2 szavazat kell)
            int votesForLaunch = (bmeVote ? 1 : 0) + (bnoVote ? 1 : 0) + (gpsVote ? 1 : 0);
            
            if (votesForLaunch >= 2) {
                currentFlightState = STATE_IN_FLIGHT;
                digitalWrite(CAM_POWER_PIN, HIGH); // KAMERA BE!
                Serial.println(">>> INDULÁS: 2/3 SZAVAZAT ÉRVÉNYES! KAMERA ON <<<");
            }
        } 
        else if (currentFlightState == STATE_IN_FLIGHT) {
            // 1. Légnyomás stabilitás landoláskor
            if (abs(currentPress - lastPressure) <= pressTolerance) {
                if (bmeStableStartTime == 0) bmeStableStartTime = millis();
                if (millis() - bmeStableStartTime > REQUIRED_STABLE_TIME) bmeVote = true;
            } else {
                bmeStableStartTime = 0;
                lastPressure = currentPress;
            }

            // 2. BNO IMU stabilitás (Nincs gyorsulás és nincs forgás 10 másodpercig)
            if (accMag < 2.0f && gyroMag < 0.5f) {
                if (bnoStableStartTime == 0) bnoStableStartTime = millis();
                if (millis() - bnoStableStartTime > REQUIRED_STABLE_TIME) bnoVote = true;
            } else {
                bnoStableStartTime = 0;
            }

            // 3. GPS stabilitás
            if (currentSats >= 4) {
                if (abs(currentGpsAlt - lastGpsAlt) <= GPS_ALT_TOLERANCE) {
                    if (gpsStableStartTime == 0) gpsStableStartTime = millis();
                    if (millis() - gpsStableStartTime > REQUIRED_STABLE_TIME) gpsVote = true;
                } else {
                    gpsStableStartTime = 0;
                    lastGpsAlt = currentGpsAlt;
                }
            } else {
                gpsStableStartTime = 0; // Ha nincs elég műhold, megszakad a számlálás
            }

            // Döntés (3-ból 2 szavazat kell a leálláshoz)
            int votesForLanding = (bmeVote ? 1 : 0) + (bnoVote ? 1 : 0) + (gpsVote ? 1 : 0);

            if (votesForLanding >= 2) {
                currentFlightState = STATE_LANDED;
                digitalWrite(CAM_POWER_PIN, LOW); // KAMERA KI!
                Serial.println(">>> LANDOLÁS: 2/3 SZAVAZAT ÉRVÉNYES! KAMERA OFF <<<");
            }
        }

        // Ciklus futtatása kb 10Hz-en (100ms)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}