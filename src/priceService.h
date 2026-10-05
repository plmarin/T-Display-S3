#ifndef PRICE_SERVICE_H
#define PRICE_SERVICE_H

#include <Arduino.h>
#include "settings.h"

#define CHART_MAX_POINTS 64
#define DAILY_MAX_DAYS   30

struct Coin
{
    const char *symbol;    // shown on screen and stored in the settings, e.g. "XRP"
    const char *id;        // CoinGecko coin id, e.g. "ripple"
};

#define COIN_COUNT 3
extern const Coin COINS[COIN_COUNT];

struct DailyBar
{
    float open, high, low, close;
    float volume;          // 24h volume at the end of the day
    float macd, signal;    // MACD(12,26) and its 9 day signal line
};

struct PriceData
{
    bool valid;            // price fields hold real data
    double price;
    double change24h;      // percent
    double marketCap;
    double volume24h;
    uint32_t updatedAt;    // epoch reported by the API
    uint32_t fetchedMs;    // millis() of the last successful price fetch

    bool chartValid;       // chart fields hold real data
    uint8_t chartCount;
    float chart[CHART_MAX_POINTS]; // 24h close prices, oldest first
    double high24h;
    double low24h;

    bool dailyValid;       // daily fields hold real data
    uint8_t dailyCount;
    DailyBar daily[DAILY_MAX_DAYS]; // UTC days, oldest first; the newest one is still open

    int lastError;         // HTTP code (or negative) of the last failed request, 0 if none
    char currency[8];      // upper case, e.g. "USD"
    const char *symbol;    // coin shown, e.g. "XRP"
    uint8_t coin;          // index in COINS
};

struct LoadProgress
{
    int done, total;       // steps loaded: price, 24h chart and daily data of every coin
    int coin;              // coin being loaded (the first one still missing data)
    int lastError;         // last error of that coin, 0 if none
    bool rateLimited;      // waiting because the API answered 429
};

// Starts the background task that polls CoinGecko. It starts on XRP and preloads every coin.
void priceServiceBegin(const TSettings &settings);

// Switches to the next coin. Data already fetched for it is shown right away.
void priceServiceNextCoin();

// How far the first load of every coin has gone
LoadProgress priceServiceProgress();

// Thread safe snapshot of the latest data for the current coin
PriceData priceServiceGet();

#endif // PRICE_SERVICE_H
