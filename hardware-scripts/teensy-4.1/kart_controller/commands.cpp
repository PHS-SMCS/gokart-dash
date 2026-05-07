#include "commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <FlexCAN_T4.h>

#include "outputs.h"
#include "parser.h"
#include "pins.h"
#include "safety.h"
#include "state.h"
#include "wheel.h"

void printHelp(Stream &out) {
  out.println("OK HELP PING|STATUS|ARM|DISARM|SAFE|OUTPUT|SPEED|BRAKE|REVERSE|CONTACTOR|THROTTLE|LED|HALL?|ESC_WRITE|ESC_READ|CAN_TX|CAN_POLL|WHEEL_BTN|WHEEL?");
}

void printStatus(Stream &out) {
  uint32_t hallCopy;
  noInterrupts();
  hallCopy = g_hallPulseCount;
  interrupts();

  out.print("OK STATUS armed=");
  out.print(isArmed() ? 1 : 0);
  out.print(" arm_ms=");
  out.print(armRemainingMs());
  out.print(" throttle_pct=");
  out.print(g_throttlePct, 2);
  out.print(" reverse=");
  out.print(g_reverseOn ? 1 : 0);
  out.print(" brake=");
  out.print(g_brakeOn ? 1 : 0);
  out.print(" speed_low=");
  out.print(g_speedLowOn ? 1 : 0);
  out.print(" speed_high=");
  out.print(g_speedHighOn ? 1 : 0);
  out.print(" cruise=");
  out.print(g_cruiseOn ? 1 : 0);
  out.print(" contactor=");
  out.print(g_contactorOn ? 1 : 0);
  out.print(" led=");
  out.print(g_ledR);
  out.print(",");
  out.print(g_ledG);
  out.print(",");
  out.print(g_ledB);
  out.print(" hall=");
  out.println(hallCopy);
}

bool applyNamedOutput(const char *name, bool asserted, Stream &out) {
  if (strcasecmp(name, "reverse") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setReverse(asserted);
  } else if (strcasecmp(name, "brake") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setBrake(asserted);
  } else if (strcasecmp(name, "speed_low") == 0) {
    if (asserted && !requireArmed(out)) return false;
    if (asserted) setSpeedHigh(false);
    setSpeedLow(asserted);
  } else if (strcasecmp(name, "speed_high") == 0) {
    if (asserted && !requireArmed(out)) return false;
    if (asserted) setSpeedLow(false);
    setSpeedHigh(asserted);
  } else if (strcasecmp(name, "cruise") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setCruise(asserted);
  } else if (strcasecmp(name, "contactor") == 0) {
    if (asserted && !requireArmed(out)) return false;
    setContactor(asserted);
  } else {
    out.println("ERR OUTPUT_NAME");
    return false;
  }

  out.print("OK OUTPUT ");
  out.print(name);
  out.print("=");
  out.println(asserted ? "on" : "off");
  return true;
}

// --- Per-command handlers --------------------------------------------------

static void cmdPing(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  out.println("OK PONG");
}

static void cmdHelp(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  printHelp(out);
}

static void cmdStatus(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  printStatus(out);
}

static void cmdArm(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR ARM seconds_required");
    return;
  }
  float seconds;
  if (!parseFloatStrict(argv[1], seconds) || seconds <= 0.0f || seconds > 30.0f) {
    out.println("ERR ARM seconds_range_0_30");
    return;
  }

  g_armUntilMs = millis() + (uint32_t)(seconds * 1000.0f);
  out.print("OK ARM seconds=");
  out.println(seconds, 2);
}

static void cmdDisarm(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  g_armUntilMs = 0;
  applySafeState();
  out.println("OK DISARMED SAFE");
}

static void cmdSafe(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  g_armUntilMs = 0;
  applySafeState();
  out.println("OK SAFE");
}

static void cmdOutput(int argc, char **argv, Stream &out) {
  if (argc < 3) {
    out.println("ERR OUTPUT usage: OUTPUT <name> <on|off>");
    return;
  }
  bool on;
  if (!parseOnOff(argv[2], on)) {
    out.println("ERR OUTPUT state_on_off");
    return;
  }
  (void)applyNamedOutput(argv[1], on, out);
}

static void cmdSpeed(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR SPEED mode_required");
    return;
  }

  String mode = String(argv[1]);
  mode.toLowerCase();
  if (mode == "low" || mode == "high") {
    if (!requireArmed(out)) return;
  }

  if (mode == "low" || mode == "medium" || mode == "high") {
    applySpeedMode(mode.c_str());
    out.print("OK SPEED mode=");
    out.println(mode);
    return;
  }

  out.println("ERR SPEED mode_low_medium_high");
}

// Shared by BRAKE / REVERSE / CONTACTOR — each is a thin alias around a
// single output setter. The name is lowercased for the setter selection,
// upperLabel is what appears after "OK " in the response.
static void cmdNamedOutputAlias(int argc, char **argv, Stream &out, const char *name, const char *upperLabel) {
  if (argc < 2) {
    out.println("ERR STATE on_off_required");
    return;
  }
  bool on;
  if (!parseOnOff(argv[1], on)) {
    out.println("ERR STATE on_off_required");
    return;
  }

  if (on && !requireArmed(out)) return;

  if (strcmp(name, "brake") == 0) setBrake(on);
  else if (strcmp(name, "reverse") == 0) setReverse(on);
  else if (strcmp(name, "contactor") == 0) setContactor(on);

  out.print("OK ");
  out.print(upperLabel);
  out.print("=");
  out.println(on ? "on" : "off");
}

static void cmdBrake(int argc, char **argv, Stream &out) {
  cmdNamedOutputAlias(argc, argv, out, "brake", "BRAKE");
}

static void cmdReverse(int argc, char **argv, Stream &out) {
  cmdNamedOutputAlias(argc, argv, out, "reverse", "REVERSE");
}

static void cmdContactor(int argc, char **argv, Stream &out) {
  cmdNamedOutputAlias(argc, argv, out, "contactor", "CONTACTOR");
}

static void cmdThrottle(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR THROTTLE percent_required");
    return;
  }

  float pct;
  if (!parseFloatStrict(argv[1], pct)) {
    out.println("ERR THROTTLE percent_number");
    return;
  }

  if (pct > 0.0f && !requireArmed(out)) {
    return;
  }

  uint16_t raw = setThrottlePercent(pct);
  out.print("OK THROTTLE pct=");
  out.print(g_throttlePct, 2);
  out.print(" dac=");
  out.println(raw);
}

static void cmdLed(int argc, char **argv, Stream &out) {
  if (argc < 4) {
    out.println("ERR LED usage: LED <r> <g> <b>");
    return;
  }

  uint32_t r, g, b;
  if (!parseUInt32Strict(argv[1], r) || !parseUInt32Strict(argv[2], g) || !parseUInt32Strict(argv[3], b)) {
    out.println("ERR LED values_0_255");
    return;
  }
  if (r > 255 || g > 255 || b > 255) {
    out.println("ERR LED values_0_255");
    return;
  }

  setLed((uint8_t)r, (uint8_t)g, (uint8_t)b);
  out.print("OK LED ");
  out.print((int)g_ledR);
  out.print(" ");
  out.print((int)g_ledG);
  out.print(" ");
  out.println((int)g_ledB);
}

static void cmdHall(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  uint32_t hallCopy;
  noInterrupts();
  hallCopy = g_hallPulseCount;
  interrupts();

  out.print("OK HALL count=");
  out.println(hallCopy);
}

static void cmdEscWrite(int argc, char **argv, Stream &out) {
  if (argc < 2) {
    out.println("ERR ESC_WRITE hexbytes_required");
    return;
  }
  if (!requireArmed(out)) return;

  uint8_t payload[64];
  int len = parseHexBytes(argv[1], payload, (int)sizeof(payload));
  if (len <= 0) {
    out.println("ERR ESC_WRITE hexbytes_invalid");
    return;
  }

  size_t written = Serial1.write(payload, (size_t)len);
  Serial1.flush();

  out.print("OK ESC_WRITE bytes=");
  out.println((int)written);
}

static void cmdEscRead(int argc, char **argv, Stream &out) {
  uint32_t maxBytes = 64;
  if (argc >= 2) {
    if (!parseUInt32Strict(argv[1], maxBytes) || maxBytes == 0) {
      out.println("ERR ESC_READ max_bytes_invalid");
      return;
    }
    if (maxBytes > 256) {
      maxBytes = 256;
    }
  }

  out.print("OK ESC_READ ");
  uint32_t count = 0;
  while (Serial1.available() && count < maxBytes) {
    uint8_t b = (uint8_t)Serial1.read();
    printHexByte(out, b);
    count++;
  }
  out.println();
}

static void cmdCanTx(int argc, char **argv, Stream &out) {
  if (argc < 3) {
    out.println("ERR CAN_TX usage: CAN_TX <id> <hexbytes>");
    return;
  }
  if (!requireArmed(out)) return;

  uint32_t id;
  if (!parseUInt32Strict(argv[1], id)) {
    out.println("ERR CAN_TX id_invalid");
    return;
  }

  uint8_t data[8];
  int len = parseHexBytes(argv[2], data, 8);
  if (len < 0) {
    out.println("ERR CAN_TX data_invalid_hex");
    return;
  }

  CAN_message_t msg;
  msg.id = id;
  msg.flags.extended = 0;
  msg.len = len;
  for (int i = 0; i < len; i++) {
    msg.buf[i] = data[i];
  }

  bool ok = Can0.write(msg);
  if (!ok) {
    out.println("ERR CAN_TX write_failed");
    return;
  }

  out.println("OK CAN_TX");
}

static void cmdCanPoll(int argc, char **argv, Stream &out) {
  uint32_t maxFrames = 8;
  if (argc >= 2) {
    if (!parseUInt32Strict(argv[1], maxFrames) || maxFrames == 0) {
      out.println("ERR CAN_POLL max_frames_invalid");
      return;
    }
    if (maxFrames > 64) {
      maxFrames = 64;
    }
  }

  uint32_t readCount = 0;
  CAN_message_t msg;
  while (readCount < maxFrames && Can0.read(msg)) {
    out.print("CAN id=0x");
    out.print(msg.id, HEX);
    out.print(" len=");
    out.print(msg.len);
    out.print(" data=");
    for (uint8_t i = 0; i < msg.len; i++) {
      printHexByte(out, msg.buf[i]);
    }
    out.println();
    readCount++;
  }

  out.print("OK CAN_POLL count=");
  out.println(readCount);
}

static void cmdWheel(int argc, char **argv, Stream &out) {
  (void)argc; (void)argv;
  out.print("OK WHEEL host_connected=");
  out.print(g_wheelHostConnected ? 1 : 0);
  if (g_wheelHostConnected) {
    out.print(" vid=0x");
    out.print(wheelJoystickVid(), HEX);
    out.print(" pid=0x");
    out.print(wheelJoystickPid(), HEX);
    out.print(" type=");
    out.print(wheelJoystickType());
  }
  out.print(" host_buttons=0x");
  out.print(g_wheelHostButtons, HEX);
  out.print(" pi_buttons=0x");
  out.println(g_wheelBtnMask, HEX);
}

static void cmdWheelBtn(int argc, char **argv, Stream &out) {
  if (argc < 3) {
    out.println("ERR WHEEL_BTN usage: WHEEL_BTN <idx> <0|1>");
    return;
  }

  uint32_t idx;
  bool pressed;
  if (!parseUInt32Strict(argv[1], idx) || idx >= WHEEL_BTN_COUNT) {
    out.print("ERR WHEEL_BTN idx_range_0_");
    out.println(WHEEL_BTN_COUNT - 1);
    return;
  }
  if (!parseOnOff(argv[2], pressed)) {
    out.println("ERR WHEEL_BTN state_0_1");
    return;
  }

  uint16_t bit = (uint16_t)1 << idx;
  if (pressed) {
    g_wheelBtnMask |= bit;
  } else {
    g_wheelBtnMask &= (uint16_t)~bit;
  }
  updateOnboardLedFromWheel();

  out.print("OK WHEEL_BTN idx=");
  out.print((int)idx);
  out.print(" state=");
  out.print(pressed ? 1 : 0);
  out.print(" mask=0x");
  out.println(g_wheelBtnMask, HEX);
}

// --- Command table ---------------------------------------------------------

const CommandHandler kCommands[] = {
  {"PING",       cmdPing},
  {"HELP",       cmdHelp},
  {"STATUS",     cmdStatus},
  {"ARM",        cmdArm},
  {"DISARM",     cmdDisarm},
  {"SAFE",       cmdSafe},
  {"OUTPUT",     cmdOutput},
  {"SPEED",      cmdSpeed},
  {"BRAKE",      cmdBrake},
  {"REVERSE",    cmdReverse},
  {"CONTACTOR",  cmdContactor},
  {"THROTTLE",   cmdThrottle},
  {"LED",        cmdLed},
  {"HALL",       cmdHall},
  {"HALL?",      cmdHall},
  {"ESC_WRITE",  cmdEscWrite},
  {"ESC_READ",   cmdEscRead},
  {"CAN_TX",     cmdCanTx},
  {"CAN_POLL",   cmdCanPoll},
  {"WHEEL",      cmdWheel},
  {"WHEEL?",     cmdWheel},
  {"WHEEL_BTN",  cmdWheelBtn},
};

const size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

void handleCommand(const String &lineIn, Stream &out) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) {
    return;
  }

  char buf[192];
  line.toCharArray(buf, sizeof(buf));

  char *tokens[10];
  int n = 0;
  char *tok = strtok(buf, " \t");
  while (tok && n < 10) {
    tokens[n++] = tok;
    tok = strtok(nullptr, " \t");
  }

  if (n == 0) {
    return;
  }

  String cmd = String(tokens[0]);
  cmd.toUpperCase();

  for (size_t i = 0; i < kCommandCount; ++i) {
    if (cmd == kCommands[i].name) {
      kCommands[i].handle(n, tokens, out);
      return;
    }
  }

  out.println("ERR UNKNOWN_CMD");
}
