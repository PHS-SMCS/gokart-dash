#include <Arduino.h>
#include <Wire.h>
#include <FlexCAN_T4.h>
#include <USBHost_t36.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// -------------------- Serial and bus settings --------------------
static constexpr uint32_t USB_BAUD = 115200;
static constexpr uint32_t PI_BAUD = 115200;
static constexpr uint32_t ESC_BAUD = 115200;
static constexpr uint32_t CAN_BAUD = 500000;

static constexpr uint8_t MCP4725_ADDR = 0x60;

// -------------------- Pin map (from SMCSKart documentation) --------------------
static constexpr uint8_t PIN_HALL_PULSES = 2;   // input from ESC pin 18
static constexpr uint8_t PIN_REVERSE = 3;       // output to ESC REV pin 8
static constexpr uint8_t PIN_BRAKE_LOW = 4;     // output to ESC low brake pin 21
static constexpr uint8_t PIN_SPEED_HIGH = 5;    // output to ESC high speed pin 3
static constexpr uint8_t PIN_SPEED_LOW = 6;     // output to ESC low speed pin 2
static constexpr uint8_t PIN_CRUISE = 9;        // output to ESC cruise/boost
static constexpr uint8_t PIN_PPS = 10;          // input from GPS PPS
static constexpr uint8_t PIN_CONTACTOR = 32;    // output to contactor trigger

// PWM LED channels (24V strip driver)
static constexpr uint8_t PIN_LED_BLUE = 33;
static constexpr uint8_t PIN_LED_GREEN = 36;
static constexpr uint8_t PIN_LED_RED = 37;

// Teensy onboard LED (used as a wheel-input heartbeat during bring-up)
static constexpr uint8_t PIN_ONBOARD_LED = LED_BUILTIN;
static constexpr uint8_t WHEEL_BTN_COUNT = 11;

// -------------------- Safety and state --------------------
static constexpr float THROTTLE_V_MIN = 0.5f;
static constexpr float THROTTLE_V_MAX = 4.3f;
static constexpr float THROTTLE_DAC_REF = 5.0f;
static constexpr uint16_t THROTTLE_DAC_MAX = 4095;

volatile uint32_t g_hallPulseCount = 0;

uint32_t g_armUntilMs = 0;
bool g_wasArmedLastLoop = false;

bool g_reverseOn = false;
bool g_brakeOn = false;
bool g_speedLowOn = false;
bool g_speedHighOn = false;
bool g_cruiseOn = false;
bool g_contactorOn = false;
float g_throttlePct = 0.0f;
uint8_t g_ledR = 0;
uint8_t g_ledG = 0;
uint8_t g_ledB = 0;

uint16_t g_wheelBtnMask = 0;

String g_usbRx;
String g_piRx;

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;

// USB Host stack for the mainboard USB-A port (Teensy 4.1 USB host header).
USBHost g_usbHost;
USBHub g_usbHub1(g_usbHost);
USBHub g_usbHub2(g_usbHost);
JoystickController g_joystick(g_usbHost);

bool g_wheelHostConnected = false;
uint32_t g_wheelHostButtons = 0;

// -------------------- Utilities --------------------
void onHallPulse() {
  g_hallPulseCount++;
}

bool isArmed() {
  return (int32_t)(g_armUntilMs - millis()) > 0;
}

uint32_t armRemainingMs() {
  if (!isArmed()) {
    return 0;
  }
  return g_armUntilMs - millis();
}

void setGroundSwitchPin(uint8_t pin, bool asserted) {
  // Hardware path is MOSFET-switched ground. Command semantics use asserted=true
  // to mean "ground/activate ESC input".
  digitalWrite(pin, asserted ? HIGH : LOW);
}

void updateOnboardLedFromWheel() {
  bool anyHeld = (g_wheelBtnMask != 0) || (g_wheelHostButtons != 0);
  digitalWrite(PIN_ONBOARD_LED, anyHeld ? HIGH : LOW);
}

void serviceUsbHostWheel() {
  g_usbHost.Task();

  bool connectedNow = (bool)g_joystick;
  if (connectedNow != g_wheelHostConnected) {
    g_wheelHostConnected = connectedNow;
    if (connectedNow) {
      char msg[80];
      snprintf(msg, sizeof(msg),
               "INFO WHEEL_HOST_CONNECTED vid=0x%04X pid=0x%04X type=%d",
               g_joystick.idVendor(), g_joystick.idProduct(),
               (int)g_joystick.joystickType());
      broadcastInfo(msg);
    } else {
      broadcastInfo("INFO WHEEL_HOST_DISCONNECTED");
      g_wheelHostButtons = 0;
      updateOnboardLedFromWheel();
    }
  }

  if (!connectedNow) {
    return;
  }

  if (g_joystick.available()) {
    g_wheelHostButtons = g_joystick.getButtons();
    updateOnboardLedFromWheel();
    g_joystick.joystickDataClear();
  }
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  g_ledR = r;
  g_ledG = g;
  g_ledB = b;
  analogWrite(PIN_LED_RED, g_ledR);
  analogWrite(PIN_LED_GREEN, g_ledG);
  analogWrite(PIN_LED_BLUE, g_ledB);
}

void writeThrottleRaw(uint16_t raw) {
  raw = raw > THROTTLE_DAC_MAX ? THROTTLE_DAC_MAX : raw;

  Wire.beginTransmission(MCP4725_ADDR);
  Wire.write(0x40);                         // Fast mode write DAC register
  Wire.write((uint8_t)(raw >> 4));          // D11..D4
  Wire.write((uint8_t)((raw & 0x0F) << 4)); // D3..D0 xxxx
  Wire.endTransmission();
}

uint16_t setThrottlePercent(float percent) {
  if (percent < 0.0f) {
    percent = 0.0f;
  }
  if (percent > 100.0f) {
    percent = 100.0f;
  }

  g_throttlePct = percent;

  float v = THROTTLE_V_MIN + ((THROTTLE_V_MAX - THROTTLE_V_MIN) * (percent / 100.0f));
  float normalized = v / THROTTLE_DAC_REF;
  if (normalized < 0.0f) {
    normalized = 0.0f;
  }
  if (normalized > 1.0f) {
    normalized = 1.0f;
  }

  uint16_t raw = (uint16_t)roundf(normalized * THROTTLE_DAC_MAX);
  writeThrottleRaw(raw);
  return raw;
}

void setReverse(bool on) {
  g_reverseOn = on;
  setGroundSwitchPin(PIN_REVERSE, on);
}

void setBrake(bool on) {
  g_brakeOn = on;
  setGroundSwitchPin(PIN_BRAKE_LOW, on);
}

void setCruise(bool on) {
  g_cruiseOn = on;
  setGroundSwitchPin(PIN_CRUISE, on);
}

void setContactor(bool on) {
  g_contactorOn = on;
  setGroundSwitchPin(PIN_CONTACTOR, on);
}

void setSpeedLow(bool on) {
  g_speedLowOn = on;
  setGroundSwitchPin(PIN_SPEED_LOW, on);
}

void setSpeedHigh(bool on) {
  g_speedHighOn = on;
  setGroundSwitchPin(PIN_SPEED_HIGH, on);
}

void applySpeedMode(const char *mode) {
  if (strcmp(mode, "low") == 0) {
    setSpeedHigh(false);
    setSpeedLow(true);
  } else if (strcmp(mode, "high") == 0) {
    setSpeedLow(false);
    setSpeedHigh(true);
  } else {
    // medium/default => both deasserted
    setSpeedLow(false);
    setSpeedHigh(false);
  }
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

void broadcastInfo(const char *msg) {
  if (Serial) {
    Serial.println(msg);
  }
  Serial2.println(msg);
}

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

  if (cmd == "PING") {
    out.println("OK PONG");
    return;
  }

  if (cmd == "HELP") {
    printHelp(out);
    return;
  }

  if (cmd == "STATUS") {
    printStatus(out);
    return;
  }

  if (cmd == "ARM") {
    if (n < 2) {
      out.println("ERR ARM seconds_required");
      return;
    }
    float seconds;
    if (!parseFloatStrict(tokens[1], seconds) || seconds <= 0.0f || seconds > 30.0f) {
      out.println("ERR ARM seconds_range_0_30");
      return;
    }

    g_armUntilMs = millis() + (uint32_t)(seconds * 1000.0f);
    out.print("OK ARM seconds=");
    out.println(seconds, 2);
    return;
  }

  if (cmd == "DISARM") {
    g_armUntilMs = 0;
    applySafeState();
    out.println("OK DISARMED SAFE");
    return;
  }

  if (cmd == "SAFE") {
    g_armUntilMs = 0;
    applySafeState();
    out.println("OK SAFE");
    return;
  }

  if (cmd == "OUTPUT") {
    if (n < 3) {
      out.println("ERR OUTPUT usage: OUTPUT <name> <on|off>");
      return;
    }
    bool on;
    if (!parseOnOff(tokens[2], on)) {
      out.println("ERR OUTPUT state_on_off");
      return;
    }
    (void)applyNamedOutput(tokens[1], on, out);
    return;
  }

  if (cmd == "SPEED") {
    if (n < 2) {
      out.println("ERR SPEED mode_required");
      return;
    }

    String mode = String(tokens[1]);
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
    return;
  }

  if (cmd == "BRAKE" || cmd == "REVERSE" || cmd == "CONTACTOR") {
    if (n < 2) {
      out.println("ERR STATE on_off_required");
      return;
    }
    bool on;
    if (!parseOnOff(tokens[1], on)) {
      out.println("ERR STATE on_off_required");
      return;
    }

    if (cmd == "BRAKE") {
      if (on && !requireArmed(out)) return;
      setBrake(on);
      out.print("OK BRAKE=");
      out.println(on ? "on" : "off");
    } else if (cmd == "REVERSE") {
      if (on && !requireArmed(out)) return;
      setReverse(on);
      out.print("OK REVERSE=");
      out.println(on ? "on" : "off");
    } else {
      if (on && !requireArmed(out)) return;
      setContactor(on);
      out.print("OK CONTACTOR=");
      out.println(on ? "on" : "off");
    }
    return;
  }

  if (cmd == "THROTTLE") {
    if (n < 2) {
      out.println("ERR THROTTLE percent_required");
      return;
    }

    float pct;
    if (!parseFloatStrict(tokens[1], pct)) {
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
    return;
  }

  if (cmd == "LED") {
    if (n < 4) {
      out.println("ERR LED usage: LED <r> <g> <b>");
      return;
    }

    uint32_t r, g, b;
    if (!parseUInt32Strict(tokens[1], r) || !parseUInt32Strict(tokens[2], g) || !parseUInt32Strict(tokens[3], b)) {
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
    return;
  }

  if (cmd == "HALL" || cmd == "HALL?") {
    uint32_t hallCopy;
    noInterrupts();
    hallCopy = g_hallPulseCount;
    interrupts();

    out.print("OK HALL count=");
    out.println(hallCopy);
    return;
  }

  if (cmd == "ESC_WRITE") {
    if (n < 2) {
      out.println("ERR ESC_WRITE hexbytes_required");
      return;
    }

    if (!requireArmed(out)) {
      return;
    }

    uint8_t payload[64];
    int len = parseHexBytes(tokens[1], payload, (int)sizeof(payload));
    if (len <= 0) {
      out.println("ERR ESC_WRITE hexbytes_invalid");
      return;
    }

    size_t written = Serial1.write(payload, (size_t)len);
    Serial1.flush();

    out.print("OK ESC_WRITE bytes=");
    out.println((int)written);
    return;
  }

  if (cmd == "ESC_READ") {
    uint32_t maxBytes = 64;
    if (n >= 2) {
      if (!parseUInt32Strict(tokens[1], maxBytes) || maxBytes == 0) {
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
    return;
  }

  if (cmd == "CAN_TX") {
    if (n < 3) {
      out.println("ERR CAN_TX usage: CAN_TX <id> <hexbytes>");
      return;
    }

    if (!requireArmed(out)) {
      return;
    }

    uint32_t id;
    if (!parseUInt32Strict(tokens[1], id)) {
      out.println("ERR CAN_TX id_invalid");
      return;
    }

    uint8_t data[8];
    int len = parseHexBytes(tokens[2], data, 8);
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
    return;
  }

  if (cmd == "CAN_POLL") {
    uint32_t maxFrames = 8;
    if (n >= 2) {
      if (!parseUInt32Strict(tokens[1], maxFrames) || maxFrames == 0) {
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
    return;
  }

  if (cmd == "WHEEL" || cmd == "WHEEL?") {
    out.print("OK WHEEL host_connected=");
    out.print(g_wheelHostConnected ? 1 : 0);
    if (g_wheelHostConnected) {
      out.print(" vid=0x");
      out.print(g_joystick.idVendor(), HEX);
      out.print(" pid=0x");
      out.print(g_joystick.idProduct(), HEX);
      out.print(" type=");
      out.print((int)g_joystick.joystickType());
    }
    out.print(" host_buttons=0x");
    out.print(g_wheelHostButtons, HEX);
    out.print(" pi_buttons=0x");
    out.println(g_wheelBtnMask, HEX);
    return;
  }

  if (cmd == "WHEEL_BTN") {
    if (n < 3) {
      out.println("ERR WHEEL_BTN usage: WHEEL_BTN <idx> <0|1>");
      return;
    }

    uint32_t idx;
    bool pressed;
    if (!parseUInt32Strict(tokens[1], idx) || idx >= WHEEL_BTN_COUNT) {
      out.print("ERR WHEEL_BTN idx_range_0_");
      out.println(WHEEL_BTN_COUNT - 1);
      return;
    }
    if (!parseOnOff(tokens[2], pressed)) {
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
    return;
  }

  out.println("ERR UNKNOWN_CMD");
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

void serviceArmTimeout() {
  bool armedNow = isArmed();
  if (g_wasArmedLastLoop && !armedNow) {
    applySafeState();
    broadcastInfo("INFO ARM_EXPIRED SAFE_APPLIED");
  }
  g_wasArmedLastLoop = armedNow;
}

void setup() {
  pinMode(PIN_HALL_PULSES, INPUT_PULLUP);
  pinMode(PIN_PPS, INPUT);

  pinMode(PIN_REVERSE, OUTPUT);
  pinMode(PIN_BRAKE_LOW, OUTPUT);
  pinMode(PIN_SPEED_HIGH, OUTPUT);
  pinMode(PIN_SPEED_LOW, OUTPUT);
  pinMode(PIN_CRUISE, OUTPUT);
  pinMode(PIN_CONTACTOR, OUTPUT);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  digitalWrite(PIN_ONBOARD_LED, LOW);

  analogWriteResolution(8);

  // Ensure deterministic safe defaults immediately
  digitalWrite(PIN_REVERSE, LOW);
  digitalWrite(PIN_BRAKE_LOW, LOW);
  digitalWrite(PIN_SPEED_HIGH, LOW);
  digitalWrite(PIN_SPEED_LOW, LOW);
  digitalWrite(PIN_CRUISE, LOW);
  digitalWrite(PIN_CONTACTOR, LOW);
  analogWrite(PIN_LED_RED, 0);
  analogWrite(PIN_LED_GREEN, 0);
  analogWrite(PIN_LED_BLUE, 0);

  Serial.begin(USB_BAUD);
  Serial1.begin(ESC_BAUD);
  Serial2.begin(PI_BAUD);

  Wire.begin();

  Can0.begin();
  Can0.setBaudRate(CAN_BAUD);

  g_usbHost.begin();

  attachInterrupt(digitalPinToInterrupt(PIN_HALL_PULSES), onHallPulse, RISING);

  applySafeState();
  g_armUntilMs = 0;
  g_wasArmedLastLoop = false;

  delay(20);
  broadcastInfo("OK BOOT kart_controller ready");
}

void loop() {
  servicePort(Serial, g_usbRx);
  servicePort(Serial2, g_piRx);
  serviceUsbHostWheel();
  serviceArmTimeout();
}
