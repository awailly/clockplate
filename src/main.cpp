// clockplate: a clock on a Soldered Inkplate 2, meant to run on USB power.
//
// The 3-color panel takes ~24s per refresh, so the clock redraws once per
// minute and starts each refresh early: it renders the *upcoming* minute so
// the panel finishes flashing right when that minute starts. No deep sleep:
// staying awake keeps the crystal-driven system clock accurate between the
// hourly SNTP re-syncs, which USB power makes affordable.
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
constexpr time_t RENDER_LEAD_S = 24;

const char *DAYS[] = {"dimanche", "lundi", "mardi", "mercredi",
                      "jeudi", "vendredi", "samedi"};
const char *MONTHS[] = {"janvier", "fevrier", "mars", "avril",
                        "mai", "juin", "juillet", "aout",
                        "septembre", "octobre", "novembre", "decembre"};

void wifiConnect()
{
    if (WiFi.status() == WL_CONNECTED)
        return;
    Serial.printf("[WIFI] connecting to %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (int i = 0; i < 100 && WiFi.status() != WL_CONNECTED; i++)
        delay(200);
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[WIFI] connected: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("[WIFI] connection failed, will retry next minute");
}

void drawCentered(const char *text, int y)
{
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
    display.setCursor((display.width() - bw) / 2 - (bx - 0), y);
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

void setup()
{
    Serial.begin(115200);
    Serial.println("\n\n[SETUP] clockplate starting");

    // begin() allocates the framebuffer in PSRAM; on failure the buffer is
    // NULL and any drawing call would crash, so restart instead.
    if (!display.begin())
    {
        Serial.println("[SETUP] display.begin() failed (framebuffer allocation or panel init)");
        delay(10000);
        ESP.restart();
    }
    display.setTextWrap(false);

    wifiConnect();
    // SNTP keeps re-syncing in the background (every hour by default)
    configTzTime(TIMEZONE_TZ, NTP_SERVER);
    Serial.println("[TIME] waiting for NTP sync...");
    struct tm t;
    while (!getLocalTime(&t, 10000))
        Serial.println("[TIME] still waiting...");
    Serial.printf("[TIME] synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
}

void loop()
{
    // next minute boundary that is at least RENDER_LEAD_S away
    time_t now = time(nullptr);
    time_t target = ((now + RENDER_LEAD_S) / 60) * 60 + 60;

    while ((now = time(nullptr)) < target - RENDER_LEAD_S)
        delay(200);

    wifiConnect(); // reconnect if the AP dropped us, keeps SNTP running

    struct tm t;
    localtime_r(&target, &t);
    drawClock(t);
}
