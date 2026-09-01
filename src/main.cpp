// clockplate: a Fibonacci clock on a Soldered Inkplate 2, battery-friendly.
//
// The face is the golden-ratio tiling of an 8x5 rectangle: five squares
// valued 1, 1, 2, 3, 5. Red squares sum to the hour (12h dial), black
// squares sum to the minutes in units of five, split squares count for
// both, white squares count for nothing. 9:35 can be "3+1 red, 5 split,
// 2 black". Most times have several valid colorings; one is picked at
// random so the face keeps changing. Nothing on the plate explains this.
//
// The 3-color panel takes ~24s per refresh, so the clock redraws once per
// 5-minute step (the face's precision) and starts each refresh early: it
// renders the *upcoming* step so the panel finishes flashing right when
// that step starts.
//
// Power model: deep sleep between refreshes, WiFi off except for an hourly
// NTP re-sync done *after* the render (so the sync never delays the step
// alignment). The ESP32 RTC slow clock drifts a few seconds per hour across
// deep sleep; each sync snaps the next step back. The ~24s of panel work
// per refresh dominates consumption and cannot be avoided.
#ifndef ARDUINO_INKPLATE2
#error "Wrong board selection for this project, please select Soldered Inkplate 2 in the boards menu."
#endif

#include <WiFi.h>
#include <esp_sntp.h>
#include <Inkplate.h>
#include "config.h"

Inkplate display;

// the face's precision: one refresh per step
constexpr time_t STEP_S = 300;
// how long before the step boundary to start refreshing the panel
constexpr time_t RENDER_LEAD_S = 26;
// woken this much earlier to absorb boot + setup time, so the render still
// starts at target - RENDER_LEAD_S
constexpr time_t BOOT_MARGIN_S = 3;
// how often to reconnect WiFi for an NTP re-sync
constexpr time_t SYNC_INTERVAL_S = 3600;

// Persistent across deep sleep, reset on power up.
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR time_t lastSyncEpoch = 0;
// the step the next wake should render, decided before going to sleep:
// recomputing it after boot would skip a step whenever the ~2s of boot
// time pushes the arithmetic past the boundary
RTC_DATA_ATTR time_t nextTarget = 0;

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
    // getLocalTime() only checks that the clock is valid, which it already
    // is on every re-sync, so it would return before the SNTP response
    // arrives and WiFi would go down without the correction ever being
    // applied: wait for the actual sync completion instead.
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTzTime(TIMEZONE_TZ, NTP_SERVER);
    bool ok = false;
    for (int i = 0; i < 100 && !ok; i++)
    {
        delay(100);
        ok = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    }
    if (ok)
    {
        lastSyncEpoch = time(nullptr);
        struct tm t;
        localtime_r(&lastSyncEpoch, &t);
        Serial.printf("[TIME] synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    }
    else
        Serial.println("[TIME] NTP sync failed");
    wifiOff();
    return clockValid();
}

// ---- Fibonacci face ----

struct FibSquare
{
    int x, y, size; // in grid units; size doubles as the square's value
};
// golden-ratio tiling of the 8x5 grid: 2 and 3 stacked on the left, the
// two 1s between them, the 5 filling the right
const FibSquare SQUARES[] = {
    {0, 0, 2}, {2, 0, 1}, {2, 1, 1}, {0, 2, 3}, {3, 0, 5}};
constexpr int NSQUARES = sizeof(SQUARES) / sizeof(SQUARES[0]);

enum Role : uint8_t
{
    OFF = 0,   // white
    HOURS,     // red
    MINUTES,   // black
    BOTH       // split red/black
};

// Picks uniformly at random (reservoir sampling over the 4^5 assignments)
// a coloring whose red+split squares sum to `hours` and black+split
// squares to `fives`. Returns the number of valid colorings; every
// reachable time has at least one since squares can serve both sums.
int pickColoring(int hours, int fives, Role out[NSQUARES])
{
    int valid = 0;
    for (int a = 0; a < 1 << (2 * NSQUARES); a++)
    {
        int h = 0, m = 0;
        for (int i = 0; i < NSQUARES; i++)
        {
            Role r = Role((a >> (2 * i)) & 3);
            if (r == HOURS || r == BOTH)
                h += SQUARES[i].size;
            if (r == MINUTES || r == BOTH)
                m += SQUARES[i].size;
        }
        if (h != hours || m != fives)
            continue;
        ++valid;
        if ((int)(esp_random() % valid) == 0)
            for (int i = 0; i < NSQUARES; i++)
                out[i] = Role((a >> (2 * i)) & 3);
    }
    return valid;
}

constexpr int UNIT = 20; // px per grid unit: 160x100 face on the 212x104 panel
constexpr int INSET = 3; // white ring between a square's frame and its fill

void drawSquare(const FibSquare &s, Role role)
{
    // center on the landscape frame; the E_INK_WIDTH/HEIGHT macros are the
    // panel's native portrait frame, only width()/height() see the rotation
    int gridX = (display.width() - 8 * UNIT) / 2;
    int gridY = (display.height() - 5 * UNIT) / 2;
    int x = gridX + s.x * UNIT, y = gridY + s.y * UNIT, w = s.size * UNIT;
    int fx = x + INSET, fy = y + INSET, fw = w - 2 * INSET;
    switch (role)
    {
    case OFF:
        break;
    case HOURS:
        display.fillRect(fx, fy, fw, fw, INKPLATE2_RED);
        break;
    case MINUTES:
        display.fillRect(fx, fy, fw, fw, INKPLATE2_BLACK);
        break;
    case BOTH:
        display.fillRect(fx, fy, fw, fw, INKPLATE2_RED);
        display.fillTriangle(fx, fy + fw - 1, fx + fw - 1, fy,
                             fx + fw - 1, fy + fw - 1, INKPLATE2_BLACK);
        break;
    }
    display.drawRect(x, y, w, w, INKPLATE2_BLACK);
}

void drawClock(const struct tm &t)
{
    int hours = t.tm_hour % 12;
    if (hours == 0)
        hours = 12;
    int fives = t.tm_min / 5;

    Role roles[NSQUARES];
    int n = pickColoring(hours, fives, roles);

    display.clearDisplay();
    for (int i = 0; i < NSQUARES; i++)
        drawSquare(SQUARES[i], roles[i]);

    const char *NAMES[] = {"off", "hour", "min", "both"};
    Serial.printf("[RENDER] %02d:%02d -> h=%d m5=%d (%d colorings) "
                  "2=%s 1a=%s 1b=%s 3=%s 5=%s\n",
                  t.tm_hour, t.tm_min, hours, fives, n,
                  NAMES[roles[0]], NAMES[roles[1]], NAMES[roles[2]],
                  NAMES[roles[3]], NAMES[roles[4]]);
    display.display();
    Serial.println("[RENDER] done");
}

// Everything runs in setup(): wake, render the upcoming step, maybe
// re-sync, deep sleep until the next step's lead time. loop() is never
// reached.
void setup()
{
    Serial.begin(115200);
    Serial.printf("\n\n[SETUP] clockplate starting, boot: %u\n", bootCount);
    ++bootCount;

    // The TZ environment variable lives in RAM and is lost across deep
    // sleep (the epoch survives, the timezone does not): restore it on
    // every boot or localtime_r falls back to UTC.
    setenv("TZ", TIMEZONE_TZ, 1);
    tzset();

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

    // use the target chosen before sleeping when it is still upcoming,
    // otherwise (fresh boot, clock jump) the next boundary at least
    // RENDER_LEAD_S away; wait out any early wake
    time_t now = time(nullptr);
    time_t target = nextTarget;
    if (target <= now || target - now > STEP_S + 60)
        target = ((now + RENDER_LEAD_S) / STEP_S) * STEP_S + STEP_S;
    while (time(nullptr) < target - RENDER_LEAD_S)
        delay(100);

    struct tm t;
    localtime_r(&target, &t);
    drawClock(t); // finishes right around `target`

    // hourly re-sync, after the render so it never delays step alignment
    if (time(nullptr) - lastSyncEpoch > SYNC_INTERVAL_S)
        ntpSync();

    nextTarget = target + STEP_S;
    deepSleepUntil(nextTarget - RENDER_LEAD_S - BOOT_MARGIN_S);
}

void loop()
{
    // Never reached: setup() always ends in deep sleep.
}
