#include <RadioLib.h>

// Lábkiosztás - Ugyanaz, mint az adónál
#define SX1280_NSS    5
#define SX1280_DIO1   34
#define SX1280_NRST   14
#define SX1280_BUSY   35

#define EBYTE_RX_EN   32 
#define EBYTE_TX_EN   33 

// PÉLDÁNYOSÍTÁS: Itt már a javított SX1280-at használjuk!
SX1280 radio = new Module(SX1280_NSS, SX1280_DIO1, SX1280_NRST, SX1280_BUSY);

// Globális változó a beérkező csomagnak
uint8_t videoPacket[126];

void setupRadio() {
  Serial.println("[Vevő] Inicializálás megkezdése...");

  radio.setRfSwitchPins(EBYTE_RX_EN, EBYTE_TX_EN);

  // A FIZIKAI BEÁLLÍTÁSOKNAK HAJSZÁLPONTOSAN EGYEZNIE KELL AZ ADÓVAL!
  int state = radio.beginFLRC(2486.0, 1300, 2, 0, 16, RADIOLIB_SHAPING_0_5);
  
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[Hiba] Rádió indulási hibakód: ");
    Serial.println(state);
    while (1);
  }
  
  // A vevőnél a kimeneti teljesítmény nem számít (hiszen csak hallgat),
  // de a biztonság kedvéért érdemes bent hagyni, ha később visszaszólna az adónak.
  radio.setOutputPower(0);
  
  // VEVŐNÉL EZ KRITIKUS: +3 dBm extra érzékenységet ad a vételhez!
  radio.setHighSensitivityMode(true);

  // Hálózati azonosító - PONTOSAN ugyanaz, mint az adónál
  uint8_t syncWord[] = { 0xC1, 0xA2, 0xB3, 0xD4 };
  radio.setSyncWord(syncWord, 4);

  radio.setCRC(2); 
  radio.variablePacketLengthMode(127);

  Serial.println("[Vevő] Sikeresen konfigurálva! Várom a csomagokat...");
}

void setup() {
  Serial.begin(115200);
  delay(1000); 
  setupRadio();
}

void loop() {
  // 1. CSOMAG FOGADÁSA
  // Ez a függvény megpróbál venni egy 126 bájtos csomagot.
  int state = radio.receive(videoPacket, 126);

  // 2. VÉTEL EREDMÉNYÉNEK FELDOLGOZÁSA
  if (state == RADIOLIB_ERR_NONE) {
    // A CSOMAG HIBÁTLANUL MEGÉRKEZETT!

    // Sorszám visszaalakítása az első 2 bájtból (bit-eltolással)
    uint16_t packetCounter = (videoPacket[0] << 8) | videoPacket[1];

    // Jelerősség mérése az adott csomagra vonatkozóan
    float rssi = radio.getRSSI();

    Serial.print("JÓ CSOMAG! Sorszám: ");
    Serial.print(packetCounter);
    Serial.print(" | Jelerősség: ");
    Serial.print(rssi);
    Serial.println(" dBm");

    // --> ITT adnád át a maradék 124 bájtot a H.264 lejátszónak/dekódernek!

  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    // A rádió vett egy csomagot a mi Sync Word-ünkkel, DE a levegőben megsérült!
    // A rádió hardveresen eldobta, így a videoPacket tömbödbe nem került fals adat.
    Serial.println("[FIGYELMEZTETÉS] CRC hiba! Sérült csomag eldobva.");

  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    // Ezt a hibát fogod látni másodpercenként rengetegszer, HA AZ ADÓ KIKAPCSOLVA VAN.
    // Ilyenkor a rádió csak vár-vár, letelik a türelmi idő, és továbbengedi a kódot.
    // Ezt kikommentezheted élesben, hogy ne floodolja a Serial monitort.
    // Serial.println("Timeout: Nem jött csomag a levegőből...");

  } else {
    // Bármilyen egyéb, ritka hardveres/SPI hiba
    Serial.print("[Hiba] Vételi hiba, kód: ");
    Serial.println(state);
  }
}