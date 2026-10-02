#ifndef PRICE_SERVICE_H
#define PRICE_SERVICE_H

#include <Arduino.h>
#include "settings.h"

#define CHART_MAX_POINTS 64

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

    int lastError;         // HTTP code (or negative) of the last failed request, 0 if none
    char currency[8];      // upper case, e.g. "USD"
};

// Starts the background task that polls CoinGecko for XRP prices
void priceServiceBegin(const TSettings &settings);

// Thread safe snapshot of the latest data
PriceData priceServiceGet();

#endif // PRICE_SERVICE_H
