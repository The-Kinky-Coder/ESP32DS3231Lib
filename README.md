# ESP32TimeSync

ESP32TimeSync keeps ESP32 system time in sync with a DS3231 RTC module while using the ESP32 time APIs for timezone and NTP handling.

## Features

- Uses DS3231 for persistence across power cycles
- Uses ESP32 SNTP via configTime
- Uses POSIX TZ strings via setenv and tzset
- Provides status flags for time validity, RTC connectivity, and NTP sync

## PlatformIO Setup

- Add this library as a dependency in your application.
- The default DS3231 library dependency is northernwidget/DS3231.
- If you need an alternative, replace it with erriez/ErriezDS3231 in platformio.ini.

## Basic Usage

```cpp
#include <ESP32TimeSync.h>

ESP32TimeSync timeSync;

void setup() {
    timeSync.begin();
    timeSync.setTimezone("PST8PDT,M3.2.0,M11.1.0");
    timeSync.enableNTP();
}

void loop() {
    timeSync.update();
    if (timeSync.isTimeValid()) {
        struct tm localTime = timeSync.getLocalTime();
    }
}
```

## POSIX TZ Strings

Examples:

- "UTC0"
- "PST8PDT,M3.2.0,M11.1.0"
- "CET-1CEST,M3.5.0,M10.5.0/3"

## Configuration

- setSyncInterval(minutes) controls how often the ESP32 checks DS3231 time.
- setDebug(true) enables Serial output for diagnostic messages.
- ESP32TIMESYNC_DEFAULT_SYNC_MINUTES can override the default sync interval.
- ESP32TIMESYNC_MAX_REASONABLE_DIFF controls the max allowed drift in seconds.
- Define ESP32TIMESYNC_DEBUG to enable debug by default.
