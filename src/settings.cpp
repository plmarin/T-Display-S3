#include "settings.h"

#include <SPIFFS.h>
#include <ArduinoJson.h>

TSettings Settings;

static bool mountFs()
{
    static bool mounted = false;
    if (!mounted)
        mounted = SPIFFS.begin(false) || SPIFFS.begin(true);
    if (!mounted)
        Serial.println("SPIFFS: mount failed");
    return mounted;
}

void sanitizeSettings(TSettings &settings)
{
    // Currency: keep letters only, lower case
    char clean[sizeof(settings.Currency)] = {0};
    size_t n = 0;
    for (size_t i = 0; settings.Currency[i] && n < sizeof(clean) - 1; i++)
    {
        char c = settings.Currency[i];
        if (isalpha((unsigned char)c))
            clean[n++] = tolower((unsigned char)c);
    }
    if (n == 0)
        strcpy(clean, DEFAULT_CURRENCY);
    strcpy(settings.Currency, clean);

    if (settings.Timezone[0] == '\0')
        strlcpy(settings.Timezone, DEFAULT_TIMEZONE, sizeof(settings.Timezone));

    if (settings.RefreshSec < MIN_REFRESH_S)
        settings.RefreshSec = MIN_REFRESH_S;
}

bool loadSettings(TSettings &settings)
{
    if (!mountFs() || !SPIFFS.exists(CONFIG_FILE))
    {
        Serial.println("Settings: no config file");
        return false;
    }

    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (!file)
        return false;

    StaticJsonDocument<256> json;
    DeserializationError error = deserializeJson(json, file);
    file.close();
    if (error)
    {
        Serial.printf("Settings: parse error %s\n", error.c_str());
        return false;
    }

    strlcpy(settings.Timezone, json["timezone"] | DEFAULT_TIMEZONE, sizeof(settings.Timezone));
    strlcpy(settings.Currency, json["currency"] | DEFAULT_CURRENCY, sizeof(settings.Currency));
    strlcpy(settings.Coin, json["coin"] | DEFAULT_COIN, sizeof(settings.Coin));
    settings.RefreshSec = json["refresh"] | DEFAULT_REFRESH_S;
    sanitizeSettings(settings);

    Serial.printf("Settings: tz=%s currency=%s coin=%s refresh=%ds\n",
                  settings.Timezone, settings.Currency, settings.Coin, settings.RefreshSec);
    return true;
}

bool saveSettings(const TSettings &settings)
{
    if (!mountFs())
        return false;

    StaticJsonDocument<256> json;
    json["timezone"] = settings.Timezone;
    json["currency"] = settings.Currency;
    json["coin"] = settings.Coin;
    json["refresh"] = settings.RefreshSec;

    File file = SPIFFS.open(CONFIG_FILE, "w");
    if (!file)
    {
        Serial.println("Settings: cannot open config file for writing");
        return false;
    }
    bool ok = serializeJson(json, file) > 0;
    file.close();
    Serial.println(ok ? "Settings: saved" : "Settings: write failed");
    return ok;
}

bool deleteSettings()
{
    return mountFs() && SPIFFS.remove(CONFIG_FILE);
}
