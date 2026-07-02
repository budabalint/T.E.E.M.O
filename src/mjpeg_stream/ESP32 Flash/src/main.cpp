#include <LittleFS.h>
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000); // várjunk, míg a soros port stabilizálódik

  Serial.println("Indulás...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }
  Serial.println("LittleFS mount OK");

  File f = LittleFS.open("/stream.mjpeg", "r");
  if (!f) {
    Serial.println("Fájl megnyitása sikertelen");
    return;
  }

  Serial.print("Fájl mérete: ");
  Serial.println(f.size());

  uint8_t buf[1024];
  size_t totalRead = 0;
  while (f.available()) {
    size_t len = f.read(buf, sizeof(buf));
    totalRead += len;
    // ... küldés hálózaton / feldolgozás
  }

  Serial.print("Beolvasott bájtok összesen: ");
  Serial.println(totalRead);

  f.close();
  Serial.println("Kész.");
}



// pio run -t erase
// pio run --target uploadfs
// pio run --target upload
void loop() {
  // üres loop is elég, nem kell a while(1) trükk
  delay(1000);
}