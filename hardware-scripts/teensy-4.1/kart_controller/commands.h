#pragma once

#include <Arduino.h>

void handleCommand(const String &lineIn, Stream &out);
void printHelp(Stream &out);
void printStatus(Stream &out);
bool applyNamedOutput(const char *name, bool asserted, Stream &out);
