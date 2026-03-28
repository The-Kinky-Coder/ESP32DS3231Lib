#ifndef ESP32TIMESYNC_H
#define ESP32TIMESYNC_H

#include <Arduino.h>
#include <time.h>
#include <freertos/portmacro.h>

#if __has_include(<DS3231.h>)
  #include <DS3231.h>
  #define ESP32TIMESYNC_DS3231_NW 1
#elif __has_include(<ErriezDS3231.h>)
  #include <ErriezDS3231.h>
  #define ESP32TIMESYNC_DS3231_ERRIEZ 1
#else
  #error "No supported DS3231 library found"
#endif

class ESP32TimeSync {
public:
    enum class ErrorCode : uint8_t {
        None = 0,
        DS3231NotConnected,
        DS3231InvalidTime,
        DS3231WriteFailed,
        NTPNotSynced,
        TimeNotValid,
        TimeDiffTooLarge
    };

    ESP32TimeSync();

    void begin(int sdaPin = 21, int sclPin = 22);

    void setTimeManual(time_t unixTime);
    void enableNTP(const char* ntpServer = "pool.ntp.org",
                   long gmtOffset_sec = 0,
                   int daylightOffset_sec = 0);
    void setTimezone(const char* tzString);

    bool isTimeValid() const;
    bool isDS3231Connected() const;
    bool isNTPSynced() const;
    ErrorCode getLastError() const;

    time_t getUnixTime() const;
    struct tm getLocalTime() const;

    void setSyncInterval(unsigned long minutes);
    void setDebug(bool enable);

    void update();

private:
#if defined(ESP32TIMESYNC_DS3231_NW)
    DS3231 _rtc;
#elif defined(ESP32TIMESYNC_DS3231_ERRIEZ)
    ErriezDS3231 _rtc;
#endif

    bool _timeValid;
    bool _ds3231Connected;
    bool _ntpSynced;
    bool _ntpEnabled;
    bool _debug;
    unsigned long _syncIntervalMs;
    unsigned long _lastSyncMs;
    volatile bool _ntpSyncReceived;
    ErrorCode _lastError;
    mutable portMUX_TYPE _lock;

    void loadFromDS3231(bool setDefaultOnInvalid);
    bool readDS3231Epoch(time_t& outEpoch);
    bool writeDS3231Epoch(time_t epoch);
    bool isEpochValid(time_t epoch) const;
    bool oscillatorRunning();
    void setSystemTime(time_t epoch, bool markValid);
    void logDebug(const char* message) const;
    void setLastError(ErrorCode code);
    bool pingDS3231();
    void setDS3231Connected(bool connected);

    static void handleNtpSync(struct timeval* tv);
    static ESP32TimeSync* _instance;
};

#endif
