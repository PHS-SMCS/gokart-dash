#include "parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "safety.h"
#include "state.h"

bool parseOnOff(const char *token, bool &out) {
  if (!token) {
    return false;
  }
  if (strcmp(token, "1") == 0 || strcasecmp(token, "on") == 0 || strcasecmp(token, "true") == 0) {
    out = true;
    return true;
  }
  if (strcmp(token, "0") == 0 || strcasecmp(token, "off") == 0 || strcasecmp(token, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

bool parseFloatStrict(const char *token, float &out) {
  if (!token) {
    return false;
  }
  char *end = nullptr;
  out = strtof(token, &end);
  return end && *end == '\0';
}

bool parseUInt32Strict(const char *token, uint32_t &out) {
  if (!token) {
    return false;
  }
  char *end = nullptr;
  unsigned long val = strtoul(token, &end, 0);
  if (!(end && *end == '\0')) {
    return false;
  }
  out = (uint32_t)val;
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

int parseHexBytes(const char *text, uint8_t *out, int maxLen) {
  if (!text) {
    return -1;
  }

  char clean[192];
  int cleanLen = 0;

  for (size_t i = 0; text[i] != '\0' && cleanLen < (int)sizeof(clean) - 1; i++) {
    char c = text[i];
    if (isxdigit((unsigned char)c)) {
      clean[cleanLen++] = c;
    }
  }
  clean[cleanLen] = '\0';

  if (cleanLen == 0 || (cleanLen % 2) != 0) {
    return -1;
  }

  int bytes = cleanLen / 2;
  if (bytes > maxLen) {
    return -2;
  }

  for (int i = 0; i < bytes; i++) {
    int hi = hexNibble(clean[i * 2]);
    int lo = hexNibble(clean[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }

  return bytes;
}

void printHexByte(Stream &out, uint8_t v) {
  static const char *kHexDigits = "0123456789ABCDEF";
  out.print(kHexDigits[(v >> 4) & 0x0F]);
  out.print(kHexDigits[v & 0x0F]);
}

bool requireArmed(Stream &out) {
  if (isArmed()) {
    return true;
  }
  out.println("ERR NOT_ARMED (send: ARM <seconds>)");
  return false;
}

void broadcastInfo(const char *msg) {
  if (Serial) {
    Serial.println(msg);
  }
  Serial2.println(msg);
}

void servicePort(Stream &port, String &buffer) {
  while (port.available() > 0) {
    char c = (char)port.read();
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        String line = buffer;
        buffer = "";
        handleCommand(line, port);
      }
    } else {
      if (buffer.length() < 180) {
        buffer += c;
      }
    }
  }
}

void onHallPulse() {
  g_hallPulseCount++;
}
