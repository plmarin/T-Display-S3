#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Config portal access point
#define DEFAULT_SSID        "XRPTickerAP"
#define DEFAULT_WIFIPW      "XRPTicker"

// User settings defaults
#define DEFAULT_TIMEZONE    "CET-1CEST,M3.5.0,M10.5.0/3"   // Spain (peninsula), POSIX TZ format
#define DEFAULT_CURRENCY    "usd"
#define DEFAULT_COIN        "XRP"
#define DEFAULT_REFRESH_S   60
#define MIN_REFRESH_S       30    // CoinGecko free API rate limit

#define CONFIG_FILE         "/xrp_config.json"

struct TSettings
{
    char Timezone[48]{ DEFAULT_TIMEZONE };
    char Currency[8]{ DEFAULT_CURRENCY };   // CoinGecko vs_currency, lower case
    char Coin[8]{ DEFAULT_COIN };           // Coin shown, switched with a double click on KEY
    int RefreshSec{ DEFAULT_REFRESH_S };
};

extern TSettings Settings;

void sanitizeSettings(TSettings &settings);
bool loadSettings(TSettings &settings);
bool saveSettings(const TSettings &settings);
bool deleteSettings();

#endif // SETTINGS_H
