// Copy this file to config.h and fill in your values. config.h is gitignored.
#pragma once
#ifndef CONFIG_H

// WiFi SSID
#define WIFI_SSID "myssid"
// WiFi password
#define WIFI_PASSWORD "mypassword"

// hostname
#define HOSTNAME "clockplate"

// NTP server to sync the clock
#define NTP_SERVER "pool.ntp.org"

// Timezone in POSIX TZ format
// Europe/Paris: "CET-1CEST,M3.5.0,M10.5.0/3"
// UTC: "UTC0"
// see https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TIMEZONE_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

// keep this to signal the program has a valid config file
#define CONFIG_H
#endif
