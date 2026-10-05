#include <Arduino.h>
#include <OneButton.h>

#include "board.h"
#include "version.h"
#include "timeconst.h"
#include "settings.h"
#include "wManager.h"
#include "priceService.h"
#include "display.h"

#define REDRAW_MS SECOND_MS

// Button 1 (BOOT): click = screen on/off, double click = rotate 180
// Button 2 (KEY):  click = next screen, double click = next coin (XRP, XLM, VELO), hold 5 s = erase config
//                  hold while booting = open config portal
OneButton button1(PIN_BUTTON_1);
OneButton button2(PIN_BUTTON_2);

static unsigned long lastDraw = 0;

static void nextCoin()
{
    strlcpy(Settings.Coin, priceServiceNextCoin(), sizeof(Settings.Coin));
    saveSettings(Settings); // Keep the coin after a restart
    displayRefresh();
}

void setup()
{
    // Display power when running from battery (LilyGo quirk)
    pinMode(PIN_ENABLE5V, OUTPUT);
    digitalWrite(PIN_ENABLE5V, HIGH);

    Serial.begin(115200);
    Serial.setTimeout(0);
    Serial.println(APP_NAME " " CURRENT_VERSION " starting...");

    button1.setPressMs(5 * SECOND_MS);
    button1.attachClick(displayToggleBacklight);
    button1.attachDoubleClick(displayFlipRotation);

    button2.setPressMs(5 * SECOND_MS);
    button2.attachClick(displayNextScreen);
    button2.attachDoubleClick(nextCoin);
    button2.attachLongPressStart(reset_configuration);

    displayInit();
    displayLoadingScreen("Iniciando...");

    init_WifiManager(); // Returns once connected (restarts otherwise)

    configTzTime(Settings.Timezone, "pool.ntp.org", "time.google.com");

    displayLoadingScreen("Obteniendo precio...");
    priceServiceBegin(Settings);
}

void loop()
{
    button1.tick();
    button2.tick();
    wifiManagerProcess();

    unsigned long now = millis();
    if (displayConsumeDirty() || now - lastDraw >= REDRAW_MS)
    {
        displayDraw();
        lastDraw = now;
    }

    delay(10);
}
