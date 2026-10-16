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
#define HOUR_MS          (60 * MINUTE_MS)
#define MIN_UPTIME_MS    HOUR_MS            // So the restart at midnight does not repeat itself in the same minute
#define NO_DATA_MS       (30 * MINUTE_MS)   // Restart if no request succeeds for this long

// Button 1 (BOOT): click = screen on/off, double click = rotate 180
// Button 2 (KEY):  click = next screen, hold ~1 s and release = next view (summary, XRP, XLM, VELO),
//                  hold 5 s = erase config, hold while booting = open config portal
OneButton button1(PIN_BUTTON_1);
OneButton button2(PIN_BUTTON_2);

static unsigned long lastDraw = 0;
static bool loading = true;   // Progress screen until the current coin has its data
static unsigned long loadStart = 0;
static unsigned long startMs = 0;      // millis() when the price service started

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
    startMs = millis();
    startLoading();
}

static void restart(const char *reason)
{
    Serial.printf("Restarting: %s\n", reason);
    displayLoadingScreen("Reiniciando...");
    delay(SECOND_MS);
    ESP.restart();
}

// Safety net in case the downloads get stuck (TLS, heap, WiFi...): starts over with everything cleared
static void checkRestart(unsigned long now)
{
    // Preventive restart every day at 00:00 local time (once the clock is synced over NTP)
    time_t t = time(nullptr);
    struct tm local;
    localtime_r(&t, &local);
    if (now >= MIN_UPTIME_MS && local.tm_year > 100 && local.tm_hour == 0 && local.tm_min == 0)
        restart("daily at 00:00");

    uint32_t lastOk = priceServiceLastOkMs();
    if (now - (lastOk ? lastOk : startMs) >= NO_DATA_MS)
        restart("no data for 30 min");
}

void loop()
{
    button1.tick();
    button2.tick();
    wifiManagerProcess();

    unsigned long now = millis();
    checkRestart(now);
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
