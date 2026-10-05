#include <Arduino.h>
#include <OneButton.h>

#include "board.h"
#include "version.h"
#include "timeconst.h"
#include "settings.h"
#include "wManager.h"
#include "priceService.h"
#include "display.h"

#define REDRAW_MS        SECOND_MS
#define LOAD_FRAME_MS    40                 // Progress bar animation
#define LOAD_TIMEOUT_MS  (3 * MINUTE_MS)    // Show the screens anyway if some data never arrives
#define COIN_PRESS_MS    800                // KEY long press that switches the view
#define RESET_PRESS_MS   (5 * SECOND_MS)    // KEY long press that erases the config

// Button 1 (BOOT): click = screen on/off, double click = rotate 180
// Button 2 (KEY):  click = next screen, hold ~1 s and release = next view (summary, XRP, XLM, VELO),
//                  hold 5 s = erase config, hold while booting = open config portal
OneButton button1(PIN_BUTTON_1);
OneButton button2(PIN_BUTTON_2);

static unsigned long lastDraw = 0;
static bool loading = true;   // Progress screen until the current coin has its data
static unsigned long loadStart = 0;

static void startLoading()
{
    loading = true;
    loadStart = millis();
}

static void nextScreen()
{
    if (!loading && priceServiceCoin() != SUMMARY)
        displayNextScreen();
}

// Also works while loading, e.g. to go back to a coin already loaded while the API is rate limited
static void nextCoin()
{
    priceServiceNextCoin();
    int coin = priceServiceCoin();
    if (coin != SUMMARY)
        displayCoinAnimation(coin); // Its data keeps downloading meanwhile, on the other core
    startLoading();                 // Ends right away if the coin already has its data
}

// The view switches on release, so holding KEY to erase the config does not switch it on the way
static void keyLongPressStop()
{
    if (button2.getPressedMs() < RESET_PRESS_MS)
        nextCoin();
}

static void keyDuringLongPress()
{
    if (button2.getPressedMs() >= RESET_PRESS_MS)
        reset_configuration(); // Restarts
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

    button2.setPressMs(COIN_PRESS_MS);
    button2.attachClick(nextScreen);
    button2.attachLongPressStop(keyLongPressStop);
    button2.attachDuringLongPress(keyDuringLongPress);

    displayInit();
    displayBootAnimation();
    displayLoadingScreen("Iniciando...");

    init_WifiManager(); // Returns once connected (restarts otherwise)

    configTzTime(Settings.Timezone, "pool.ntp.org", "time.google.com");

    priceServiceBegin(Settings);
    startLoading();
}

void loop()
{
    button1.tick();
    button2.tick();
    wifiManagerProcess();

    unsigned long now = millis();
    if (loading)
    {
        // Only when a coin is shown before its charts finished downloading in the background
        LoadProgress progress = priceServiceProgress();
        loading = progress.done < progress.total && now - loadStart < LOAD_TIMEOUT_MS;
        if (!loading)
        {
            Serial.printf("Loading done in %lu ms (%d/%d)\n", now - loadStart, progress.done, progress.total);
            displayRefresh();
        }
        else if (now - lastDraw >= LOAD_FRAME_MS)
        {
            displayLoadProgress(progress);
            lastDraw = now;
        }
    }

    if (!loading && (displayConsumeDirty() || now - lastDraw >= REDRAW_MS))
    {
        displayDraw();
        lastDraw = now;
    }

    delay(10);
}
