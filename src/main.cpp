// clockplate: a clock on a Soldered Inkplate 2, battery-friendly.
//
// The 3-color panel takes ~24s per refresh, so the clock redraws once per
// minute and starts each refresh early: it renders the *upcoming* minute so
// the panel finishes flashing right when that minute starts.
//
// Power model: deep sleep between refreshes, WiFi off except for an hourly
// NTP re-sync done *after* the render (so the sync never delays the minute
// alignment). The ESP32 RTC slow clock drifts a few seconds per hour across
// deep sleep; each sync snaps the next minute back. The ~24s of panel work
// per minute dominates consumption and cannot be avoided.
#ifndef ARDUINO_INKPLATE2
#error "Wrong board selection for this project, please select Soldered Inkplate 2 in the boards menu."
#endif

#include <WiFi.h>
#include <Inkplate.h>
#include "config.h"
#include "fonts/Roboto_12.h"
#include "fonts/Roboto_Medium_38.h" // digits and colon only, for the time

Inkplate display;

// how long before the minute boundary to start refreshing the panel
constexpr time_t RENDER_LEAD_S = 26;
// how often to reconnect WiFi for an NTP re-sync
constexpr time_t SYNC_INTERVAL_S = 3600;

// Persistent across deep sleep, reset on power up.
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR time_t lastSyncEpoch = 0;

const char *DAYS[] = {"dimanche", "lundi", "mardi", "mercredi",
                      "jeudi", "vendredi", "samedi"};
const char *MONTHS[] = {"janvier", "fevrier", "mars", "avril",
                        "mai", "juin", "juillet", "aout",
                        "septembre", "octobre", "novembre", "decembre"};

bool clockValid()
{
    return time(nullptr) > 1600000000; // not 1970: NTP has run at least once
}

void deepSleepUntil(time_t target)
{
    long secs = target - time(nullptr);
    if (secs < 1)
        secs = 1;
    Serial.printf("[SLEEP] deep sleep for %lds\n", secs);
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    esp_deep_sleep_start();
}

bool wifiConnect()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    Serial.printf("[WIFI] connecting to %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (int i = 0; i < 75 && WiFi.status() != WL_CONNECTED; i++)
        delay(200);
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WIFI] connection failed");
        return false;
    }
    Serial.printf("[WIFI] connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void wifiOff()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// Returns true if the system clock is usable afterwards.
bool ntpSync()
{
    if (!wifiConnect())
    {
        wifiOff();
        return clockValid(); // keep drifting on the old sync if we have one
    }
    configTzTime(TIMEZONE_TZ, NTP_SERVER);
    struct tm t;
    bool ok = getLocalTime(&t, 10000);
    if (ok)
    {
        lastSyncEpoch = time(nullptr);
        Serial.printf("[TIME] synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    }
    else
        Serial.println("[TIME] NTP sync failed");
    wifiOff();
    return clockValid();
}

void drawCentered(const char *text, int y)
{
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
    display.setCursor((display.width() - bw) / 2 - bx, y);
    display.print(text);
}

void drawClock(const struct tm &t)
{
    char line[32];
    display.clearDisplay();

    display.setFont(&Roboto_Medium38pt7b);
    display.setTextColor(INKPLATE2_BLACK);
    snprintf(line, sizeof(line), "%02d:%02d", t.tm_hour, t.tm_min);
    drawCentered(line, 62); // digits are 54px tall, top lands at y=8

    display.setFont(&Roboto_12);
    display.setTextColor(INKPLATE2_RED);
    snprintf(line, sizeof(line), "%s %d %s", DAYS[t.tm_wday], t.tm_mday,
             MONTHS[t.tm_mon]);
    drawCentered(line, 96);

    Serial.printf("[RENDER] %02d:%02d (%s)\n", t.tm_hour, t.tm_min, line);
    display.display();
    Serial.println("[RENDER] done");
}

// Everything runs in setup(): wake, render the upcoming minute, maybe
// re-sync, deep sleep until the next minute's lead time. loop() is never
// reached.
void setup()
{
    Serial.begin(115200);
    Serial.printf("\n\n[SETUP] clockplate starting, boot: %u\n", bootCount);
    ++bootCount;

    // fresh power on: the clock is unusable until a first NTP sync
    if (!clockValid() && !ntpSync())
    {
        Serial.println("[SETUP] no valid clock, retrying in 60s");
        deepSleepUntil(time(nullptr) + 60);
    }

    // begin() allocates the framebuffer in PSRAM; on failure the buffer is
    // NULL and any drawing call would crash, so sleep and retry instead.
    if (!display.begin())
    {
        Serial.println("[SETUP] display.begin() failed (framebuffer allocation or panel init)");
        deepSleepUntil(time(nullptr) + 60);
    }
    display.setTextWrap(false);

    // next minute boundary at least RENDER_LEAD_S away, wait out any early wake
    time_t now = time(nullptr);
    time_t target = ((now + RENDER_LEAD_S) / 60) * 60 + 60;
    while (time(nullptr) < target - RENDER_LEAD_S)
        delay(100);

    struct tm t;
    localtime_r(&target, &t);
    drawClock(t); // finishes right around `target`

    // hourly re-sync, after the render so it never delays minute alignment
    if (time(nullptr) - lastSyncEpoch > SYNC_INTERVAL_S)
        ntpSync();

    deepSleepUntil(target + 60 - RENDER_LEAD_S);
}

void loop()
{
    // Never reached: setup() always ends in deep sleep.
}
