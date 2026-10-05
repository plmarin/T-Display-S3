#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "priceService.h"

void displayInit();
void displayBootAnimation();          // ~3.4 s: the coin logos drop in, then the name and a ticker line appear
void displayCoinAnimation(int coin);  // ~1.2 s: the logo of the coin switched to drops in, then its symbol
void displayLoadingScreen(const char *status);
void displaySetupScreen(const char *apName, const char *apPassword);
void displayLoadProgress(const LoadProgress &progress);   // coin logo and progress bar while loading

// Summary of every coin, or the cyclic screens of one coin (price, 24h chart, daily candles, daily MACD,
// clock, market)
void displayDraw();
void displayNextScreen();
void displayRefresh();        // redraw as soon as possible
bool displayConsumeDirty();   // true once after something requests an immediate redraw

void displayToggleBacklight();
void displayFlipRotation();

#endif // DISPLAY_H
