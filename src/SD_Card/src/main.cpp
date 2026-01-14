#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h> // ESP32 fájlrendszer kezelő

// --- A TE LÁBKIOSZTÁSOD ---
const int PIN_CS   = 10;
const int PIN_MOSI = 11;
const int PIN_MISO = 13;
const int PIN_SCK  = 12;

void setup() {
  Serial.begin(4000000);
  while (!Serial) { delay(10); } // Várunk a Serialra

  Serial.println("\n--- SD.h Teszt Inditasa ---");

  // 1. SPI beállítása manuálisan
  // A sorrend ESP32-nél: sck, miso, mosi, ss
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

  // 2. SD kártya indítása
  // Átadjuk neki a CS lábat és a már beállított SPI buszt
  if (!SD.begin(PIN_CS, SPI)) {
    Serial.println("HIBA: SD kartya nem indult el!");
    Serial.println("- Ellenorizd a kabelt!");
    Serial.println("- Ellenorizd, hogy FAT32-re van-e formazva!");
    return;
  }
  
  Serial.println("KARTYA OK! Tipusanak lekerdezese...");
  
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
    Serial.println("Hiba: Nem csatlakozik kartya.");
    return;
  }

  // 3. Fájl írás teszt
  Serial.println("Fajl letrehozasa: /teszt_sd.txt");
  
  // FILE_WRITE = létrehozza, vagy ha létezik, a végére ír
  File myFile = SD.open("/teszt_sd.txt", FILE_WRITE);

  if (false) {
    Serial.print("Iras a fajlba...");
    myFile.println("Szia! Ez egy teszt iras az SD.h konyvtarral.");
    myFile.println("Mukodik a kartya a 45-os labon!");
    
    // FONTOS: Bezáráskor mentődik el ténylegesen
    myFile.close();
    Serial.println(" KESZ.");
    Serial.println("Most mar kiveheted a kartyat es megnezheted gepen.");
  } else {
    Serial.println("HIBA: Nem sikerult megnyitni a fajlt irasra.");
  }
}

void loop() {
  // Nem csinálunk semmit
}