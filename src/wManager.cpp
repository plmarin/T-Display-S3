#include <WiFi.h>
#include <WiFiManager.h>

#include "wManager.h"
#include "board.h"
#include "settings.h"
#include "display.h"
#include "timeconst.h"
#include "version.h"

static WiFiManager wm;
static bool shouldSaveConfig = false;

static void saveConfigCallback()
{
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

static void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.printf("Config portal started: SSID %s, IP %s\n",
                  myWiFiManager->getConfigPortalSSID().c_str(), WiFi.softAPIP().toString().c_str());
    displaySetupScreen(myWiFiManager->getConfigPortalSSID().c_str(), DEFAULT_WIFIPW);
}

void reset_configuration()
{
    Serial.println("Erasing config, restarting");
    displayLoadingScreen("Borrando configuracion...");
    deleteSettings();
    wm.resetSettings();
    delay(SECOND_MS);
    ESP.restart();
}

void init_WifiManager()
{
    bool forceConfig = false;

    // Holding the KEY button while booting opens the config portal with the current settings
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    if (!digitalRead(PIN_BUTTON_2))
    {
        Serial.println("Button pressed to force config mode");
        forceConfig = true;
    }

    WiFi.mode(WIFI_STA);

    // First boot (or after a reset): ask for the settings
    if (!loadSettings(Settings))
        forceConfig = true;

    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setSaveParamsCallback(saveConfigCallback);
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalBlocking(true);
    wm.setConnectTimeout(40);
    wm.setConfigPortalTimeout(180);
    wm.setTitle(APP_NAME);

    WiFiManagerParameter header_html("<hr><h3>" APP_NAME "</h3>");

    WiFiManagerParameter currency_box("currency", "Moneda (usd, eur, gbp...)", Settings.Currency, sizeof(Settings.Currency) - 1);

    char refreshValue[8];
    snprintf(refreshValue, sizeof(refreshValue), "%d", Settings.RefreshSec);
    WiFiManagerParameter refresh_box("refresh", "Refresco del precio en segundos (min. 30)", refreshValue, 5);

    WiFiManagerParameter tz_box("timezone", "Zona horaria (formato POSIX TZ)", Settings.Timezone, sizeof(Settings.Timezone) - 1);
    WiFiManagerParameter tz_help("<small>Peninsula: CET-1CEST,M3.5.0,M10.5.0/3<br>Canarias: WET0WEST,M3.5.0/1,M10.5.0<br>UTC: UTC0</small>");

    wm.addParameter(&header_html);
    wm.addParameter(&currency_box);
    wm.addParameter(&refresh_box);
    wm.addParameter(&tz_box);
    wm.addParameter(&tz_help);

    if (forceConfig)
    {
        displaySetupScreen(DEFAULT_SSID, DEFAULT_WIFIPW);
        wm.setBreakAfterConfig(true); // Return after saving, even if the WiFi connection fails
        wm.startConfigPortal(DEFAULT_SSID, DEFAULT_WIFIPW);
    }
    else
    {
        displayLoadingScreen("Conectando WiFi...");
        wm.autoConnect(DEFAULT_SSID, DEFAULT_WIFIPW);
    }

    if (shouldSaveConfig)
    {
        strlcpy(Settings.Currency, currency_box.getValue(), sizeof(Settings.Currency));
        Settings.RefreshSec = atoi(refresh_box.getValue());
        strlcpy(Settings.Timezone, tz_box.getValue(), sizeof(Settings.Timezone));
        sanitizeSettings(Settings);
        saveSettings(Settings);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected, restarting");
        displayLoadingScreen("Sin WiFi. Reiniciando...");
        delay(3 * SECOND_MS);
        ESP.restart();
    }

    Serial.printf("WiFi connected, IP %s\n", WiFi.localIP().toString().c_str());
}

void wifiManagerProcess()
{
    static wl_status_t oldStatus = WL_IDLE_STATUS;

    wm.process();

    wl_status_t newStatus = WiFi.status();
    if (newStatus != oldStatus)
    {
        if (newStatus == WL_CONNECTED)
            Serial.println("CONNECTED - Current ip: " + WiFi.localIP().toString());
        else
            Serial.printf("[Error] - WiFi status: %d\n", newStatus);
        oldStatus = newStatus;
    }
}
