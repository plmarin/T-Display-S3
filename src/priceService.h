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
#define SUMMARY    -1      // "Coin" of the summary view, which shows every coin
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

struct CoinSummary
{
    bool valid;            // price fields hold real data
    double price;
    double change24h;      // percent
    double marketCap;
    uint32_t fetchedMs;    // millis() of the last successful price fetch
    bool chartValid;       // chart holds the 24h closes
    bool dailyValid;
    uint8_t chartCount;
    float chart[CHART_MAX_POINTS];
};

struct Summary
{
    CoinSummary coins[COIN_COUNT];
    char currency[8];
    int chartsLoaded;      // 24h and daily charts already downloaded, out of 2 * COIN_COUNT
    int lastError;         // last error of any request, 0 if none
    int waitSec;           // seconds left before retrying after a 429, 0 if not rate limited
};

struct LoadProgress
{
    int done, total;       // steps loaded for the current coin: price, 24h chart and daily data
    int coin;              // current coin
    int lastError;         // last error of that coin, 0 if none
    int waitSec;           // seconds left before retrying after a 429, 0 if not rate limited
};

// Starts the background task that polls CoinGecko, starting on the summary
void priceServiceBegin(const TSettings &settings);

// Cycles summary -> XRP -> XLM -> VELO -> summary. The charts of every coin download in the background,
// a coin shown before that loads them right away.
void priceServiceNextCoin();

// Current coin index, or SUMMARY
int priceServiceCoin();

// Thread safe snapshot of every coin for the summary view
Summary priceServiceSummary();

// How far the data of the current coin has loaded
LoadProgress priceServiceProgress();

// Thread safe snapshot of the latest data for the current coin
PriceData priceServiceGet();

#endif // PRICE_SERVICE_H
