#include <Arduino.h>

int counter = 0;
int step_value = 2; // Ezt az értéket fogjuk debuggolás közben vizsgálni

void setup() {
  // A soros port inicializálása
  Serial.begin(115200);
  
  // Várunk egy kicsit, hogy az USB eszköz feléledjen a PC-n
  delay(2000); 
  
  Serial.println("ESP32-S3 Natív USB JTAG/Serial teszt indítása!");
}

void loop() {
  counter += step_value;
  
  Serial.print("Ciklus számláló értéke: ");
  Serial.println(counter);

  // Ide (a 22. sor környékére) tegyél egy breakpoint-ot a PlatformIO-ban!
  delay(1000); 
}