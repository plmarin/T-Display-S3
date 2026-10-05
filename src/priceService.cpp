#include "priceService.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "timeconst.h"
#include "version.h"

// CoinGecko public API (no key needed, ~10-30 calls/min)
#define API_BASE            "https://api.coingecko.com/api/v3"

#define CHART_REFRESH_MS    (10 * MINUTE_MS)
#define DAILY_REFRESH_MS    (30 * MINUTE_MS)
#define DAILY_HISTORY_DAYS  60            // Hourly samples; the extra days warm up the MACD averages
#define DAILY_DOC_SIZE      (256 * 1024)  // ~1440 hourly prices + volumes, kept in PSRAM
#define DAY_MS              86400000.0
#define RETRY_MS            (30 * SECOND_MS)
#define RATE_LIMIT_MS       (2 * MINUTE_MS)

#define ERR_PARSE           -100
#define ERR_DATA            -101

const Coin COINS[COIN_COUNT] = {
    {"XRP", "ripple"},
    {"XLM", "stellar"},
    {"VELO", "velo"},
};

// Waits `wait` ms from `start`; a zero wait is due right away
struct Timer
{
    uint32_t start = 0;
    uint32_t wait = 0;
};

struct CoinTimers
{
    Timer price, chart, daily;
};

static SemaphoreHandle_t s_mutex;
static PriceData s_data[COIN_COUNT];   // Cached per coin, so switching back shows data right away
static CoinTimers s_timers[COIN_COUNT];
static Timer s_rateLimit;              // A 429 pauses every request, whatever the coin
static volatile int s_coin = 0;
static String s_vs;
static uint32_t s_refreshMs;

static bool due(const Timer &timer)
{
    return millis() - timer.start >= timer.wait;
}

static void schedule(Timer &timer, uint32_t wait)
{
    timer.start = millis();
    timer.wait = wait;
}

// Big JSON documents go to PSRAM instead of the internal heap
struct SpiRamAllocator
{
    void *allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void *ptr) { heap_caps_free(ptr); }
    void *reallocate(void *ptr, size_t size) { return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM); }
};
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

static int fetchJson(const String &url, JsonDocument &doc, const JsonDocument *filter = nullptr)
{
    WiFiClientSecure client;
    client.setInsecure(); // Public market data only, no credentials involved

    HTTPClient http;
    http.setTimeout(10 * SECOND_MS);
    http.useHTTP10(true); // No chunked encoding, so the stream can be parsed directly
    if (!http.begin(client, url))
        return HTTPC_ERROR_CONNECTION_REFUSED;

    http.setUserAgent(APP_NAME "/" CURRENT_VERSION);
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
        DeserializationError error = filter
            ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter->as<JsonVariantConst>()))
            : deserializeJson(doc, http.getStream());
        if (error)
        {
            Serial.printf("Price: JSON error %s\n", error.c_str());
            code = ERR_PARSE;
        }
    }
    http.end();
    return code;
}

static void setError(int c, int code)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data[c].lastError = code;
    xSemaphoreGive(s_mutex);
}

static int fetchPrice(int c)
{
    const char *id = COINS[c].id;
    String url = API_BASE "/simple/price?ids=" + String(id) + "&vs_currencies=" + s_vs +
                 "&include_market_cap=true&include_24hr_vol=true&include_24hr_change=true"
                 "&include_last_updated_at=true&precision=full";

    StaticJsonDocument<512> doc;
    int code = fetchJson(url, doc);
    if (code != HTTP_CODE_OK)
        return code;

    JsonObject coin = doc[id];
    if (coin.isNull() || !coin[s_vs].is<double>())
        return ERR_DATA;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    PriceData &data = s_data[c];
    data.price = coin[s_vs].as<double>();
    data.change24h = coin[s_vs + "_24h_change"] | 0.0;
    data.marketCap = coin[s_vs + "_market_cap"] | 0.0;
    data.volume24h = coin[s_vs + "_24h_vol"] | 0.0;
    data.updatedAt = coin["last_updated_at"] | 0;
    data.fetchedMs = millis();
    data.valid = true;
    data.lastError = 0;
    xSemaphoreGive(s_mutex);

    Serial.printf("Price: %s = %.8g %s (%+.2f%%)\n", COINS[c].symbol, coin[s_vs].as<double>(), s_data[c].currency,
                  coin[s_vs + "_24h_change"] | 0.0);
    return code;
}

static int fetchChart(int c)
{
    // 48 candles of 30 minutes: [timestamp, open, high, low, close]
    String url = API_BASE "/coins/" + String(COINS[c].id) + "/ohlc?days=1&precision=full&vs_currency=" + s_vs;

    DynamicJsonDocument doc(16384);
    int code = fetchJson(url, doc);
    if (code != HTTP_CODE_OK)
        return code;

    JsonArray candles = doc.as<JsonArray>();
    size_t total = candles.size();
    if (total < 2)
        return ERR_DATA;

    double high = candles[0][2].as<double>();
    double low = candles[0][3].as<double>();
    for (JsonVariant candle : candles)
    {
        high = max(high, candle[2].as<double>());
        low = min(low, candle[3].as<double>());
    }

    // Downsample evenly if needed, always keeping the newest candle
    size_t count = min(total, (size_t)CHART_MAX_POINTS);
    float points[CHART_MAX_POINTS];
    for (size_t i = 0; i < count; i++)
    {
        size_t idx = (count == 1) ? total - 1 : i * (total - 1) / (count - 1);
        points[i] = candles[idx][4].as<float>();
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    PriceData &data = s_data[c];
    memcpy(data.chart, points, sizeof(float) * count);
    data.chartCount = count;
    data.high24h = high;
    data.low24h = low;
    data.chartValid = true;
    xSemaphoreGive(s_mutex);

    Serial.printf("Price: %s chart updated, %u points, high %.8g low %.8g\n", COINS[c].symbol, (unsigned)count, high, low);
    return code;
}

static int fetchDaily(int c)
{
    // Hourly samples for 2-90 days: [timestamp ms, value]
    String url = API_BASE "/coins/" + String(COINS[c].id) + "/market_chart?days=" + String(DAILY_HISTORY_DAYS) +
                 "&precision=full&vs_currency=" + s_vs;

    StaticJsonDocument<64> filter;
    filter["prices"] = true;
    filter["total_volumes"] = true;

    SpiRamJsonDocument doc(DAILY_DOC_SIZE);
    int code = fetchJson(url, doc, &filter);
    if (code != HTTP_CODE_OK)
        return code;

    JsonArray prices = doc["prices"];
    JsonArray volumes = doc["total_volumes"];
    if (prices.size() < 2)
        return ERR_DATA;

    // Group the hourly prices into UTC day candles
    const int maxDays = DAILY_HISTORY_DAYS + 4;
    DailyBar days[maxDays];
    long dayIds[maxDays];
    int count = 0;
    for (JsonArray sample : prices)
    {
        long id = (long)(sample[0].as<double>() / DAY_MS);
        float value = sample[1].as<float>();
        if (count == 0 || id != dayIds[count - 1])
        {
            if (count == maxDays) // Keep the newest days
            {
                memmove(days, days + 1, sizeof(DailyBar) * (maxDays - 1));
                memmove(dayIds, dayIds + 1, sizeof(long) * (maxDays - 1));
                count--;
            }
            days[count] = {value, value, value, value, 0, 0, 0};
            dayIds[count++] = id;
        }
        else
        {
            DailyBar &day = days[count - 1];
            day.high = max(day.high, value);
            day.low = min(day.low, value);
            day.close = value;
        }
    }
    if (count < 2)
        return ERR_DATA;

    // total_volumes is a rolling 24h volume: keep the last sample of each day
    int j = 0;
    for (JsonArray sample : volumes)
    {
        long id = (long)(sample[0].as<double>() / DAY_MS);
        while (j < count - 1 && dayIds[j] < id)
            j++;
        if (dayIds[j] == id)
            days[j].volume = sample[1].as<float>();
    }

    // MACD(12,26,9) over the daily closes
    float ema12 = days[0].close, ema26 = days[0].close, signal = 0;
    for (int i = 0; i < count; i++)
    {
        float close = days[i].close;
        ema12 += (close - ema12) * 2 / 13;
        ema26 += (close - ema26) * 2 / 27;
        float macd = ema12 - ema26;
        signal = i == 0 ? macd : signal + (macd - signal) * 2 / 10;
        days[i].macd = macd;
        days[i].signal = signal;
    }

    int shown = min(count, DAILY_MAX_DAYS);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    PriceData &data = s_data[c];
    memcpy(data.daily, days + count - shown, sizeof(DailyBar) * shown);
    data.dailyCount = shown;
    data.dailyValid = true;
    xSemaphoreGive(s_mutex);

    Serial.printf("Price: %s daily updated, %d days, MACD %.6g signal %.6g\n",
                  COINS[c].symbol, shown, days[count - 1].macd, days[count - 1].signal);
    return code;
}

static uint32_t nextDelay(int c, int code, uint32_t okDelay)
{
    if (code == HTTP_CODE_OK)
        return okDelay;
    Serial.printf("Price: %s request failed (%d)\n", COINS[c].symbol, code);
    setError(c, code);
    if (code == HTTP_CODE_TOO_MANY_REQUESTS)
    {
        schedule(s_rateLimit, RATE_LIMIT_MS);
        return RATE_LIMIT_MS;
    }
    return RETRY_MS;
}

// Runs one request if due; returns false when rate limited, so the rest wait too
static bool poll(int c, Timer &timer, int (*fetch)(int), uint32_t okDelay)
{
    if (!due(s_rateLimit))
        return false;
    if (due(timer))
        schedule(timer, nextDelay(c, fetch(c), okDelay));
    return true;
}

static void priceTask(void *)
{
    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            // Only the coin on screen is polled
            int c = s_coin;
            CoinTimers &timers = s_timers[c];
            poll(c, timers.price, fetchPrice, s_refreshMs) &&
                poll(c, timers.chart, fetchChart, CHART_REFRESH_MS) &&
                poll(c, timers.daily, fetchDaily, DAILY_REFRESH_MS);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void priceServiceBegin(const TSettings &settings)
{
    s_mutex = xSemaphoreCreateMutex();

    s_vs = settings.Currency;
    s_refreshMs = settings.RefreshSec * SECOND_MS;
    String upper = s_vs;
    upper.toUpperCase();
    for (int c = 0; c < COIN_COUNT; c++)
    {
        memset(&s_data[c], 0, sizeof(PriceData));
        strlcpy(s_data[c].currency, upper.c_str(), sizeof(s_data[c].currency));
        s_data[c].symbol = COINS[c].symbol;
        if (strcasecmp(settings.Coin, COINS[c].symbol) == 0)
            s_coin = c;
    }

    // TLS needs a generous stack; run on core 0 next to the WiFi stack so the UI loop stays responsive
    xTaskCreatePinnedToCore(priceTask, "Price", 16384, NULL, 1, NULL, 0);
}

const char *priceServiceNextCoin()
{
    s_coin = (s_coin + 1) % COIN_COUNT;
    Serial.printf("Price: switched to %s\n", COINS[s_coin].symbol);
    return COINS[s_coin].symbol;
}

PriceData priceServiceGet()
{
    PriceData copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_data[s_coin];
    xSemaphoreGive(s_mutex);
    return copy;
}
