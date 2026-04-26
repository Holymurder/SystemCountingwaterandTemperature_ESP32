#pragma once
#include <Arduino.h>
#include <Keypad.h>
#include "display.h"   // for MenuState

extern Keypad keypad;

void handleKey(char key);
bool handleNumericInput(char key, MenuState cancelTarget);
