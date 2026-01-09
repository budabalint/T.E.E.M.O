#include <Arduino.h>
#include <CanSat.h>

CanSat canSat;

void setup() {
  canSat.begin();
  delay(5000);
}

void loop() { 
  float hofok = canSat._bme.readTemperature();
  float para = canSat._bme.readHumidity();
  float nyomas = canSat._bme.readPressure();

  Serial.print("Homerseklet: ");
  Serial.print(hofok);
  Serial.println(" C");

  Serial.print("Paratartalom: ");
  Serial.print(para);
  Serial.println(" %");

  Serial.print("Nyomas: ");
  Serial.print(nyomas);
  Serial.println(" Pa");

    canSat._bno.update();

    if (canSat._bno.hasNewData()) {
        
        float roll = canSat._bno.getRoll();
        float pitch = canSat._bno.getPitch();
        float yaw = canSat._bno.getYaw();

        Vector3 linAccel = canSat._bno.getLinearAcceleration();
        Vector3 gyro = canSat._bno.getGyroscope();
        Vector3 gravity = canSat._bno.getGravity();
        Vector3 mag = canSat._bno.getMagnetometer();

        Serial.println("========================================");
        
        Serial.println("[ ORIENTACIO ]");
        Serial.print("Roll:  "); Serial.print(roll); Serial.println(" deg");
        Serial.print("Pitch: "); Serial.print(pitch); Serial.println(" deg");
        Serial.print("Yaw:   "); Serial.print(yaw); Serial.println(" deg (Eszak)");
        Serial.println();

        Serial.println("[ LINEARIS GYORSULAS ]");
        Serial.print("X: "); Serial.print(linAccel.x); Serial.println(" m/s^2");
        Serial.print("Y: "); Serial.print(linAccel.y); Serial.println(" m/s^2");
        Serial.print("Z: "); Serial.print(linAccel.z); Serial.println(" m/s^2");
        Serial.println();

        Serial.println("[ GIROSZKOP ]");
        Serial.print("X: "); Serial.print(gyro.x); Serial.println(" rad/s");
        Serial.print("Y: "); Serial.print(gyro.y); Serial.println(" rad/s");
        Serial.print("Z: "); Serial.print(gyro.z); Serial.println(" rad/s");
        Serial.println();

        Serial.println("[ GRAVITACIO ]");
        Serial.print("X: "); Serial.print(gravity.x); Serial.println(" m/s^2");
        Serial.print("Y: "); Serial.print(gravity.y); Serial.println(" m/s^2");
        Serial.print("Z: "); Serial.print(gravity.z); Serial.println(" m/s^2");
        Serial.println();

        Serial.println("[ MAGNETOMETER ]");
        Serial.print("X: "); Serial.print(mag.x); Serial.println(" uT");
        Serial.print("Y: "); Serial.print(mag.y); Serial.println(" uT");
        Serial.print("Z: "); Serial.print(mag.z); Serial.println(" uT");
        
        Serial.println("========================================");
        Serial.println();
    }

  float lux = canSat._veml.readLux();
  uint16_t white = canSat._veml.readWhite();

  Serial.print("Fenyero (lux): ");
  Serial.print(lux);
  Serial.println(" lx");

  Serial.print("Feher feny (RAW): ");
  Serial.println(white);




  Serial.println("----------------------------");

  
  delay(1000);


}

