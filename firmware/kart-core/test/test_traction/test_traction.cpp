// Host-side tests for the traction-track helpers: ARM chord detector, hall
// speed estimator, and main-contactor sequencer.
#include <unity.h>

#include "arm_chord.h"
#include "contactor_seq.h"
#include "hall_speed.h"

void setUp() {}
void tearDown() {}

// -------------------- ArmChord --------------------

namespace {
kart::ArmChordInputs chord_held() {
  return kart::ArmChordInputs{true, true, true, true};
}
}  // namespace

void test_chord_fires_after_hold() {
  kart::ArmChord c(1000);
  auto in = chord_held();
  TEST_ASSERT_FALSE(c.update(in, 0));     // start forming
  TEST_ASSERT_FALSE(c.update(in, 500));   // not yet
  TEST_ASSERT_FALSE(c.update(in, 999));   // not yet
  TEST_ASSERT_TRUE(c.update(in, 1000));   // one-shot fire
  TEST_ASSERT_FALSE(c.update(in, 1100));  // does not re-fire while held
}

void test_chord_restarts_if_broken() {
  kart::ArmChord c(1000);
  auto in = chord_held();
  c.update(in, 0);
  in.paddle_right = false;  // released a paddle
  TEST_ASSERT_FALSE(c.update(in, 500));
  in = chord_held();        // re-form; timer restarts
  TEST_ASSERT_FALSE(c.update(in, 600));
  TEST_ASSERT_FALSE(c.update(in, 1599));
  TEST_ASSERT_TRUE(c.update(in, 1600));
}

void test_chord_requires_brake_and_throttle_released() {
  kart::ArmChord c(1000);
  kart::ArmChordInputs in{true, true, false, true};  // no brake
  c.update(in, 0);
  TEST_ASSERT_FALSE(c.update(in, 2000));
  in = kart::ArmChordInputs{true, true, true, false};  // throttle not released
  c.update(in, 3000);
  TEST_ASSERT_FALSE(c.update(in, 5000));
}

void test_chord_must_release_before_rearm() {
  kart::ArmChord c(1000);
  auto in = chord_held();
  c.update(in, 0);
  TEST_ASSERT_TRUE(c.update(in, 1000));
  // Held continuously -> never re-fires.
  TEST_ASSERT_FALSE(c.update(in, 2000));
  TEST_ASSERT_FALSE(c.update(in, 3000));
  // Release fully, then re-form -> fires again.
  kart::ArmChordInputs released{false, false, false, true};
  c.update(released, 3100);
  in = chord_held();
  c.update(in, 3200);
  TEST_ASSERT_TRUE(c.update(in, 4200));
}

// -------------------- HallSpeed --------------------

void test_hall_starts_stopped() {
  kart::HallSpeed h;
  h.update(0, 0);
  TEST_ASSERT_TRUE(h.stopped(0));
  TEST_ASSERT_EQUAL_UINT16(0, h.hz_x10());
}

void test_hall_reports_stopped_after_timeout() {
  kart::HallSpeed h(100, 300);
  h.update(0, 0);
  h.update(5, 50);  // pulses arriving
  TEST_ASSERT_FALSE(h.stopped(50));
  // No new pulses for >300 ms -> stopped.
  h.update(5, 400);
  TEST_ASSERT_TRUE(h.stopped(400));
}

void test_hall_frequency_over_window() {
  kart::HallSpeed h(100, 300);
  h.update(0, 0);
  // 50 pulses in 100 ms = 500 Hz -> hz_x10 = 5000.
  h.update(50, 100);
  TEST_ASSERT_EQUAL_UINT16(5000, h.hz_x10());
  TEST_ASSERT_FALSE(h.stopped(100));
}

// -------------------- ContactorSequencer --------------------

kart::ContactorConfig prechargeCfg() {
  kart::ContactorConfig cfg;
  cfg.precharge_ms = 2000;
  cfg.precharge_max_ms = 3000;
  cfg.precharge_cooldown_ms = 10000;
  cfg.settle_ms = 300;
  cfg.has_bus_sense = false;
  return cfg;
}

void test_contactor_precharges_then_closes_and_readies() {
  kart::ContactorSequencer s(prechargeCfg());

  s.update(false, 0);
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());

  s.update(true, 100);  // engage -> precharge, contactor still OPEN
  TEST_ASSERT_TRUE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());

  s.update(true, 2000);  // 1900 ms in: still precharging, still open
  TEST_ASSERT_TRUE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());
  TEST_ASSERT_EQUAL_UINT32(1900, s.precharge_elapsed_ms(2000));

  s.update(true, 2100);  // 2000 ms elapsed -> resistor off, contactor closes
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_TRUE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.ready());

  s.update(true, 2300);  // mid-settle
  TEST_ASSERT_FALSE(s.ready());

  s.update(true, 2400);  // settle elapsed (>=300 ms)
  TEST_ASSERT_TRUE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_TRUE(s.ready());
  TEST_ASSERT_FALSE(s.faulted());
}

// The resistor is never energized at the same time as the contactor, in any
// phase, across a whole engage cycle.
void test_precharge_never_overlaps_contactor() {
  kart::ContactorSequencer s(prechargeCfg());
  for (uint32_t t = 0; t <= 5000; t += 10) {
    s.update(true, t);
    TEST_ASSERT_FALSE(s.precharge_on() && s.contactor_closed());
  }
}

// A starved loop (no update for longer than the hard cap) must shed the
// resistor and fault, not close the contactor.
void test_precharge_overrun_faults() {
  kart::ContactorSequencer s(prechargeCfg());
  s.update(true, 0);
  TEST_ASSERT_TRUE(s.precharge_on());
  s.update(true, 5500);  // > precharge_max_ms
  TEST_ASSERT_TRUE(s.faulted());
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());
}

// Rapid arm/disarm cycling must not duty-cycle the resistor: a re-engage inside
// the cooldown waits with everything open instead of re-precharging.
void test_precharge_cooldown_blocks_rapid_recycle() {
  kart::ContactorSequencer s(prechargeCfg());
  s.update(true, 0);
  s.update(true, 2100);  // precharge done, contactor closed
  s.update(false, 2200);  // disarm
  TEST_ASSERT_FALSE(s.contactor_closed());

  s.update(true, 2300);  // re-engage 200 ms later -> cooldown, nothing energized
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.ready());

  s.update(true, 12200);  // cooldown elapsed -> precharge may run again
  TEST_ASSERT_TRUE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());
}

void test_contactor_opens_on_disengage() {
  kart::ContactorSequencer s(prechargeCfg());
  s.update(true, 0);
  s.update(true, 2100);
  s.update(true, 3000);
  TEST_ASSERT_TRUE(s.contactor_closed());
  s.update(false, 3100);
  TEST_ASSERT_FALSE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_FALSE(s.ready());
}

// Disengaging mid-precharge drops the resistor immediately.
void test_disengage_mid_precharge_drops_resistor() {
  kart::ContactorSequencer s(prechargeCfg());
  s.update(true, 0);
  TEST_ASSERT_TRUE(s.precharge_on());
  s.update(false, 500);
  TEST_ASSERT_FALSE(s.precharge_on());
  TEST_ASSERT_FALSE(s.contactor_closed());
}

void test_contactor_bus_sense_ready_and_fault() {
  kart::ContactorConfig cfg = prechargeCfg();
  cfg.settle_ms = 500;
  cfg.has_bus_sense = true;
  kart::ContactorSequencer s(cfg);

  // Bus charged by precharge: ready as soon as sense confirms after the
  // contactor closes, before the settle timeout.
  s.update(true, 0, /*bus_voltage_ok=*/true);
  s.update(true, 2000, /*bus_voltage_ok=*/true);  // precharge done, closing
  s.update(true, 2050, /*bus_voltage_ok=*/true);
  TEST_ASSERT_TRUE(s.ready());

  // Fresh sequencer, bus never comes up -> fault at settle timeout.
  kart::ContactorSequencer s2(cfg);
  s2.update(true, 0, /*bus_voltage_ok=*/false);
  s2.update(true, 2000, /*bus_voltage_ok=*/false);
  s2.update(true, 2600, /*bus_voltage_ok=*/false);
  TEST_ASSERT_TRUE(s2.faulted());
  TEST_ASSERT_FALSE(s2.contactor_closed());
  // Dropping engage clears the latched fault.
  s2.update(false, 700);
  TEST_ASSERT_FALSE(s2.faulted());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_chord_fires_after_hold);
  RUN_TEST(test_chord_restarts_if_broken);
  RUN_TEST(test_chord_requires_brake_and_throttle_released);
  RUN_TEST(test_chord_must_release_before_rearm);
  RUN_TEST(test_hall_starts_stopped);
  RUN_TEST(test_hall_reports_stopped_after_timeout);
  RUN_TEST(test_hall_frequency_over_window);
  RUN_TEST(test_contactor_precharges_then_closes_and_readies);
  RUN_TEST(test_precharge_never_overlaps_contactor);
  RUN_TEST(test_precharge_overrun_faults);
  RUN_TEST(test_precharge_cooldown_blocks_rapid_recycle);
  RUN_TEST(test_contactor_opens_on_disengage);
  RUN_TEST(test_disengage_mid_precharge_drops_resistor);
  RUN_TEST(test_contactor_bus_sense_ready_and_fault);
  return UNITY_END();
}
