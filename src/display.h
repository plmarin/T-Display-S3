#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

void displayInit();
void displayLoadingScreen(const char *status);
void displaySetupScreen(const char *apName, const char *apPassword);

// Cyclic screens (price, 24h chart, clock, Madrid calendar, market)
void displayDraw();
void displayNextScreen();
bool displayConsumeDirty();   // true once after something requests an immediate redraw

void displayToggleBacklight();
void displayFlipRotation();

#endif // DISPLAY_H
