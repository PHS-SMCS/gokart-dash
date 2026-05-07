#include "state.h"

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
uint8_t g_ledManualR = 0;
uint8_t g_ledManualG = 0;
uint8_t g_ledManualB = 0;

String g_usbRx;
String g_piRx;

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> Can0;
