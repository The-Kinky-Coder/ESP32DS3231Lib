#include "ESP32TimeSync.h"
#include <esp_log.h>
static const char ESP32TIMESYNC_TAG[] = "ESP32TimeSync";

#include <Wire.h>
#include <stdlib.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <freertos/portmacro.h>

#ifndef ESP32TIMESYNC_DEFAULT_SYNC_MINUTES
#define ESP32TIMESYNC_DEFAULT_SYNC_MINUTES 60UL
#endif

#ifndef ESP32TIMESYNC_MAX_REASONABLE_DIFF
#define ESP32TIMESYNC_MAX_REASONABLE_DIFF 5
#endif

static const time_t kDefaultEpoch = 1735689600; // 2025-01-01 00:00:00 UTC
static const time_t kMinEpoch = 1577836800;     // 2020-01-01 00:00:00 UTC
static const time_t kMaxEpoch = 4102444800;     // 2100-01-01 00:00:00 UTC

ESP32TimeSync* ESP32TimeSync::_instance = nullptr;

ESP32TimeSync::ESP32TimeSync()
    : _timeValid(false),
      _ds3231Connected(false),
      _ntpSynced(false),
      _ntpEnabled(false),
      _debug(false),
      _syncIntervalMs(ESP32TIMESYNC_DEFAULT_SYNC_MINUTES * 60UL * 1000UL),
      _lastSyncMs(0),
      _ntpSyncReceived(false),
    _lastError(ErrorCode::None),
    _lock(portMUX_INITIALIZER_UNLOCKED) {}

void ESP32TimeSync::begin(int sdaPin, int sclPin) {
    Wire.begin(sdaPin, sclPin);

    if (_instance == nullptr) {
        _instance = this;
    }

#ifdef ESP32TIMESYNC_DEBUG
    _debug = true;
#endif

    loadFromDS3231(true);
}

void ESP32TimeSync::setTimeManual(time_t unixTime) {
    setSystemTime(unixTime, true);

    if (!writeDS3231Epoch(unixTime)) {
        setLastError(ErrorCode::DS3231WriteFailed);
    }
}

void ESP32TimeSync::enableNTP(const char* ntpServer, long gmtOffset_sec, int daylightOffset_sec) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    sntp_set_time_sync_notification_cb(&ESP32TimeSync::handleNtpSync);
    _ntpEnabled = true;
}

void ESP32TimeSync::setTimezone(const char* tzString) {
    if (tzString == nullptr) {
        return;
    }
    setenv("TZ", tzString, 1);
    tzset();
}

bool ESP32TimeSync::isTimeValid() const {
    portENTER_CRITICAL(&_lock);
    bool result = _timeValid;
    portEXIT_CRITICAL(&_lock);
    return result;
}

bool ESP32TimeSync::isDS3231Connected() const {
    portENTER_CRITICAL(&_lock);
    bool result = _ds3231Connected;
    portEXIT_CRITICAL(&_lock);
    return result;
}

bool ESP32TimeSync::isNTPSynced() const {
    portENTER_CRITICAL(&_lock);
    bool result = _ntpSynced;
    portEXIT_CRITICAL(&_lock);
    return result;
}

ESP32TimeSync::ErrorCode ESP32TimeSync::getLastError() const {
    portENTER_CRITICAL(&_lock);
    ErrorCode result = _lastError;
    portEXIT_CRITICAL(&_lock);
    return result;
}

time_t ESP32TimeSync::getUnixTime() const {
    return time(nullptr);
}

struct tm ESP32TimeSync::getLocalTime() const {
    time_t now = time(nullptr);
    struct tm localTime;
    localtime_r(&now, &localTime);
    return localTime;
}

void ESP32TimeSync::setSyncInterval(unsigned long minutes) {
    if (minutes == 0) {
        minutes = 1;
    }
    _syncIntervalMs = minutes * 60UL * 1000UL;
}

void ESP32TimeSync::setDebug(bool enable) {
    _debug = enable;
}

void ESP32TimeSync::update() {
    bool handleNtp = false;
    portENTER_CRITICAL(&_lock);
    if (_ntpSyncReceived) {
        _ntpSyncReceived = false;
        handleNtp = true;
    }
    portEXIT_CRITICAL(&_lock);

    if (handleNtp) {
        time_t now = time(nullptr);
        if (isEpochValid(now)) {
            setSystemTime(now, true);
            if (!writeDS3231Epoch(now)) {
                setLastError(ErrorCode::DS3231WriteFailed);
                logDebug("DS3231 write failed after NTP sync.");
            }
            portENTER_CRITICAL(&_lock);
            _ntpSynced = true;
            portEXIT_CRITICAL(&_lock);
            _lastSyncMs = millis();
        } else {
            setLastError(ErrorCode::NTPNotSynced);
            logDebug("NTP sync reported invalid time.");
        }
    }

    if (millis() - _lastSyncMs >= _syncIntervalMs) {
        _lastSyncMs = millis();
        time_t dsEpoch = 0;
        if (readDS3231Epoch(dsEpoch)) {
            time_t espEpoch = time(nullptr);
            long diff = labs((long)(dsEpoch - espEpoch));
            if (diff <= ESP32TIMESYNC_MAX_REASONABLE_DIFF) {
                bool markValid = isTimeValid();
                setSystemTime(dsEpoch, markValid);
                setLastError(ErrorCode::None);
            } else {
                setLastError(ErrorCode::TimeDiffTooLarge);
                logDebug("DS3231 time difference too large. Skipping sync.");
            }
        } else {
            setLastError(ErrorCode::DS3231NotConnected);
            logDebug("DS3231 not connected. Using ESP32 internal clock.");
        }
    }
}

void ESP32TimeSync::loadFromDS3231(bool setDefaultOnInvalid) {
    time_t dsEpoch = 0;
    if (readDS3231Epoch(dsEpoch) && isEpochValid(dsEpoch) && oscillatorRunning()) {
        setSystemTime(dsEpoch, true);
        setLastError(ErrorCode::None);
    } else {
        if (setDefaultOnInvalid) {
            setSystemTime(kDefaultEpoch, false);
        }
        setLastError(ErrorCode::DS3231InvalidTime);
    }
}

bool ESP32TimeSync::readDS3231Epoch(time_t& outEpoch) {
    if (!pingDS3231()) {
        setDS3231Connected(false);
        return false;
    }
#if defined(ESP32TIMESYNC_DS3231_NW)
    DateTime now = RTClib::now(_rtc._Wire);
    outEpoch = static_cast<time_t>(now.unixtime());
    setDS3231Connected(true);
    return true;
#elif defined(ESP32TIMESYNC_DS3231_ERRIEZ)
    outEpoch = _rtc.getEpoch();
    setDS3231Connected(true);
    return true;
#else
    setDS3231Connected(false);
    return false;
#endif
}

bool ESP32TimeSync::writeDS3231Epoch(time_t epoch) {
    if (!pingDS3231()) {
        setDS3231Connected(false);
        return false;
    }
#if defined(ESP32TIMESYNC_DS3231_NW)
    _rtc.setEpoch(epoch, false);
    setDS3231Connected(true);
    return true;
#elif defined(ESP32TIMESYNC_DS3231_ERRIEZ)
    _rtc.setEpoch(epoch);
    setDS3231Connected(true);
    return true;
#else
    setDS3231Connected(false);
    return false;
#endif
}

bool ESP32TimeSync::isEpochValid(time_t epoch) const {
    if (epoch == 0) {
        return false;
    }
    return epoch >= kMinEpoch && epoch < kMaxEpoch;
}

bool ESP32TimeSync::oscillatorRunning() {
#if defined(ESP32TIMESYNC_DS3231_NW)
    return _rtc.oscillatorCheck();
#elif defined(ESP32TIMESYNC_DS3231_ERRIEZ)
    return true;
#else
    return false;
#endif
}

void ESP32TimeSync::setSystemTime(time_t epoch, bool markValid) {
    struct timeval tv = {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    portENTER_CRITICAL(&_lock);
    _timeValid = markValid;
    portEXIT_CRITICAL(&_lock);
}

void ESP32TimeSync::logDebug(const char* message) const {
    if (_debug && message != nullptr) {
        ESP_LOGI(ESP32TIMESYNC_TAG, "%s", message);
    }
}

void ESP32TimeSync::setLastError(ErrorCode code) {
    portENTER_CRITICAL(&_lock);
    _lastError = code;
    portEXIT_CRITICAL(&_lock);
}

void ESP32TimeSync::handleNtpSync(struct timeval* tv) {
    (void)tv;
    if (_instance != nullptr) {
        _instance->_ntpSyncReceived = true;
    }
}

bool ESP32TimeSync::pingDS3231() {
    Wire.beginTransmission(0x68);
    return Wire.endTransmission() == 0;
}

void ESP32TimeSync::setDS3231Connected(bool connected) {
    portENTER_CRITICAL(&_lock);
    _ds3231Connected = connected;
    portEXIT_CRITICAL(&_lock);
}
