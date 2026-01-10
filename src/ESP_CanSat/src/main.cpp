#include <Arduino.h>
#include <CanSat.h>

CanSat canSat;

void setup() {
  canSat.begin();
  delay(5000);
}

void loop() {
  canSat._bno.update();
  canSat._gps.encode();

  float hofok = canSat._bme.readTemperature();
  float para = canSat._bme.readHumidity();
  float nyomas = canSat._bme.readPressure();
  float lux = canSat._veml.readLux();
  uint16_t white = canSat._veml.readWhite();

  bool iaqSuccess = canSat._sgp.measure();
  bool rawSuccess = canSat._sgp.measureRaw();

  Serial.println(F("\n==================== [ KORNYEZETI ADATOK ] ===================="));
  
  Serial.print(F("Homerseklet:  ")); Serial.print(hofok); Serial.print(F(" C"));
  Serial.print(F("\t\tParatartalom: ")); Serial.print(para); Serial.println(F(" %"));
  
  Serial.print(F("Nyomas:       ")); Serial.print(nyomas); Serial.print(F(" Pa"));
  Serial.print(F("\tFenyero:      ")); Serial.print(lux); Serial.println(F(" lx"));
  
  Serial.print(F("Feher feny:   ")); Serial.println(white);

  if (iaqSuccess && rawSuccess) {
    Serial.println(F("\n---------------------- [ LEVEGOMINOSEG ] ----------------------"));
    Serial.print(F("eCO2:   ")); Serial.print(canSat._sgp.GetCo2()); Serial.print(F(" ppm"));
    Serial.print(F("\t\tTVOC:   ")); Serial.print(canSat._sgp.GetTVOC()); Serial.println(F(" ppb"));
    
    Serial.print(F("H2 Raw: ")); Serial.print(canSat._sgp.GetH2());
    Serial.print(F("\t\tEth Raw: ")); Serial.println(canSat._sgp.GetEtanol());
  } else {
    Serial.println(F("\n[!] SGP szenzor hiba"));
  }

  if (canSat._bno.hasNewData()) {
    float roll = canSat._bno.getRoll();
    float pitch = canSat._bno.getPitch();
    float yaw = canSat._bno.getYaw();
    Vector3 linAccel = canSat._bno.getLinearAcceleration();
    Vector3 gyro = canSat._bno.getGyroscope();
    Vector3 gravity = canSat._bno.getGravity();
    Vector3 mag = canSat._bno.getMagnetometer();

    Serial.println(F("\n--------------------- [ MOZGAS ES POZICIO ] -------------------"));
    Serial.print(F("Orientacio:   ")); 
    Serial.print(F("Roll: ")); Serial.print(roll);
    Serial.print(F(" | Pitch: ")); Serial.print(pitch);
    Serial.print(F(" | Yaw: ")); Serial.println(yaw);

    Serial.print(F("Lin. Gyors.:  ")); 
    Serial.print(F("X: ")); Serial.print(linAccel.x); 
    Serial.print(F(" | Y: ")); Serial.print(linAccel.y); 
    Serial.print(F(" | Z: ")); Serial.println(linAccel.z);

    Serial.print(F("Giroszkop:    ")); 
    Serial.print(F("X: ")); Serial.print(gyro.x); 
    Serial.print(F(" | Y: ")); Serial.print(gyro.y); 
    Serial.print(F(" | Z: ")); Serial.println(gyro.z);

    Serial.print(F("Magnetometer: ")); 
    Serial.print(F("X: ")); Serial.print(mag.x); 
    Serial.print(F(" | Y: ")); Serial.print(mag.y); 
    Serial.print(F(" | Z: ")); Serial.println(mag.z);
    
    Serial.print(F("Gravitacio:   ")); 
    Serial.print(F("X: ")); Serial.print(gravity.x); 
    Serial.print(F(" | Y: ")); Serial.print(gravity.y); 
    Serial.print(F(" | Z: ")); Serial.println(gravity.z);
  }

  if (canSat._gps.isUpdated()) {
    Serial.println(F("\n-------------------------- [ GPS ] ----------------------------"));
    Serial.print(F("Lat: ")); Serial.print(canSat._gps.getLat(), 7);
    Serial.print(F("\tLng: ")); Serial.print(canSat._gps.getLng(), 7);
    Serial.print(F("\tSat: ")); Serial.println(canSat._gps.getSatellites());
    
    Serial.print(F("Alt: ")); Serial.print(canSat._gps.getAltitude()); Serial.print(F(" m"));
    Serial.print(F("\tSpd: ")); Serial.print(canSat._gps.getSpeed()); Serial.println(F(" km/h"));
  }

  Serial.println(F("===============================================================\n"));
  
  delay(1000);
}
