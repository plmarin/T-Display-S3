#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "priceService.h"

void displayInit();
void displayLoadingScreen(const char *status);
void displaySetupScreen(const char *apName, const char *apPassword);
void displayLoadProgress(const LoadProgress &progress);   // coin logo and progress bar while loading

// Cyclic screens (price, 24h chart, daily candles, daily MACD, clock, market)
void displayDraw();
void displayNextScreen();
void displayRefresh();        // redraw as soon as possible
bool displayConsumeDirty();   // true once after something requests an immediate redraw

void displayToggleBacklight();
void displayFlipRotation();

#endif // DISPLAY_H
