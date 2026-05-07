#include "safety.h"

#include "outputs.h"
#include "wheel.h"
#include "state.h"

// Forward declaration; provided by parser.h after Task 7.
void broadcastInfo(const char *msg);

bool isArmed() {
  return (int32_t)(g_armUntilMs - millis()) > 0;
}

uint32_t armRemainingMs() {
  if (!isArmed()) {
    return 0;
  }
  return g_armUntilMs - millis();
}

void applySafeState() {
  setReverse(false);
  setBrake(false);
  setCruise(false);
  setContactor(false);
  applySpeedMode("medium");
  setThrottlePercent(0.0f);
  setLed(0, 0, 0);
}

void serviceArmTimeout() {
  bool armedNow = isArmed();
  if (g_wasArmedLastLoop && !armedNow) {
    applySafeState();
    broadcastInfo("INFO ARM_EXPIRED SAFE_APPLIED");
  }
  g_wasArmedLastLoop = armedNow;
}
