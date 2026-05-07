#pragma once

#include <Arduino.h>

bool parseOnOff(const char *token, bool &out);
bool parseFloatStrict(const char *token, float &out);
bool parseUInt32Strict(const char *token, uint32_t &out);

int hexNibble(char c);
int parseHexBytes(const char *text, uint8_t *out, int maxLen);

void printHexByte(Stream &out, uint8_t v);

bool requireArmed(Stream &out);
void broadcastInfo(const char *msg);

// Read characters from `port`, accumulating into `buffer`. On newline/return,
// invoke handleCommand(buffer, port) and clear the buffer.
void servicePort(Stream &port, String &buffer);

// Hall-pulse ISR. Attach via attachInterrupt(...).
void onHallPulse();
