#pragma once

#include <Arduino.h>

bool isArmed();
uint32_t armRemainingMs();

// Drive every safety-gated output to a deterministic safe state.
void applySafeState();

// Watchdog: if the ARM window expired this loop, apply safe state and broadcast.
void serviceArmTimeout();
