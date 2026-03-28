#include <Arduino.h>
#include <ESP32TimeSync.h>

ESP32TimeSync timeSync;

void setup() {
    Serial.begin(115200);

    timeSync.begin();
    timeSync.setTimezone("PST8PDT,M3.2.0,M11.1.0");
    timeSync.enableNTP("pool.ntp.org", 0, 0);
    timeSync.setSyncInterval(60);
    timeSync.setDebug(true);
}

void loop() {
    timeSync.update();

    if (!timeSync.isTimeValid()) {
        Serial.println("Time not yet valid. Waiting for NTP or manual set.");
        delay(1000);
        return;
    }

    struct tm localTime = timeSync.getLocalTime();
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    Serial.print("Local time: ");
    Serial.println(buffer);

    if (!timeSync.isDS3231Connected()) {
        Serial.println("DS3231 not connected. Using ESP32 internal clock.");
    }

    delay(1000);
}
