#pragma once

#include <Arduino.h>

void handleCommand(const String &lineIn, Stream &out);
void printHelp(Stream &out);
void printStatus(Stream &out);
bool applyNamedOutput(const char *name, bool asserted, Stream &out);

struct CommandHandler {
  const char *name;                                  // uppercase command name
  void (*handle)(int argc, char **argv, Stream &out);
};

extern const CommandHandler kCommands[];
extern const size_t kCommandCount;
