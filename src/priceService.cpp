#include "priceService.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "timeconst.h"
#include "version.h"

// CoinGecko public API (no key needed, ~10-30 calls/min)
#define API_BASE            "https://api.coingecko.com/api/v3"
#define COIN_ID             "ripple"

#define CHART_REFRESH_MS    (10 * MINUTE_MS)
#define RETRY_MS            (30 * SECOND_MS)
#define RATE_LIMIT_MS       (2 * MINUTE_MS)

#define ERR_PARSE           -100
#define ERR_DATA            -101

static SemaphoreHandle_t s_mutex;
static PriceData s_data;
static String s_vs;
static uint32_t s_refreshMs;

static int fetchJson(const String &url, JsonDocument &doc)
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
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (error)
        {
            Serial.printf("Price: JSON error %s\n", error.c_str());
            code = ERR_PARSE;
        }
    }
    http.end();
    return code;
}

static void setError(int code)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data.lastError = code;
    xSemaphoreGive(s_mutex);
}

static int fetchPrice()
{
    String url = API_BASE "/simple/price?ids=" COIN_ID "&vs_currencies=" + s_vs +
                 "&include_market_cap=true&include_24hr_vol=true&include_24hr_change=true"
                 "&include_last_updated_at=true&precision=full";

    StaticJsonDocument<512> doc;
    int code = fetchJson(url, doc);
    if (code != HTTP_CODE_OK)
        return code;

    JsonObject coin = doc[COIN_ID];
    if (coin.isNull() || !coin[s_vs].is<double>())
        return ERR_DATA;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_data.price = coin[s_vs].as<double>();
    s_data.change24h = coin[s_vs + "_24h_change"] | 0.0;
    s_data.marketCap = coin[s_vs + "_market_cap"] | 0.0;
    s_data.volume24h = coin[s_vs + "_24h_vol"] | 0.0;
    s_data.updatedAt = coin["last_updated_at"] | 0;
    s_data.fetchedMs = millis();
    s_data.valid = true;
    s_data.lastError = 0;
    xSemaphoreGive(s_mutex);

    Serial.printf("Price: XRP = %.6f %s (%+.2f%%)\n", s_data.price, s_data.currency, s_data.change24h);
    return code;
}

static int fetchChart()
{
    // 48 candles of 30 minutes: [timestamp, open, high, low, close]
    String url = API_BASE "/coins/" COIN_ID "/ohlc?days=1&precision=full&vs_currency=" + s_vs;

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
    memcpy(s_data.chart, points, sizeof(float) * count);
    s_data.chartCount = count;
    s_data.high24h = high;
    s_data.low24h = low;
    s_data.chartValid = true;
    xSemaphoreGive(s_mutex);

    Serial.printf("Price: chart updated, %u points, high %.6f low %.6f\n", (unsigned)count, high, low);
    return code;
}

static uint32_t nextDelay(int code, uint32_t okDelay)
{
    if (code == HTTP_CODE_OK)
        return okDelay;
    Serial.printf("Price: request failed (%d)\n", code);
    setError(code);
    return code == HTTP_CODE_TOO_MANY_REQUESTS ? RATE_LIMIT_MS : RETRY_MS;
}

static void priceTask(void *)
{
    uint32_t nextPrice = millis();
    uint32_t nextChart = millis();

    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            if ((int32_t)(millis() - nextPrice) >= 0)
                nextPrice = millis() + nextDelay(fetchPrice(), s_refreshMs);

            if ((int32_t)(millis() - nextChart) >= 0)
                nextChart = millis() + nextDelay(fetchChart(), CHART_REFRESH_MS);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void priceServiceBegin(const TSettings &settings)
{
    s_mutex = xSemaphoreCreateMutex();
    memset(&s_data, 0, sizeof(s_data));

    s_vs = settings.Currency;
    s_refreshMs = settings.RefreshSec * SECOND_MS;
    String upper = s_vs;
    upper.toUpperCase();
    strlcpy(s_data.currency, upper.c_str(), sizeof(s_data.currency));

    // TLS needs a generous stack; run on core 0 next to the WiFi stack so the UI loop stays responsive
    xTaskCreatePinnedToCore(priceTask, "Price", 16384, NULL, 1, NULL, 0);
}

PriceData priceServiceGet()
{
    PriceData copy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    copy = s_data;
    xSemaphoreGive(s_mutex);
    return copy;
}
