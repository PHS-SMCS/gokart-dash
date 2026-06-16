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

void test_contactor_closes_and_ready_after_settle() {
  kart::ContactorConfig cfg;
  cfg.settle_ms = 300;
  cfg.has_bus_sense = false;
  kart::ContactorSequencer s(cfg);

  s.update(false, 0);
  TEST_ASSERT_FALSE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.ready());

  s.update(true, 100);  // engage -> close now, settling
  TEST_ASSERT_TRUE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.ready());

  s.update(true, 300);  // mid-settle
  TEST_ASSERT_FALSE(s.ready());

  s.update(true, 400);  // settle elapsed (>=300 ms)
  TEST_ASSERT_TRUE(s.contactor_closed());
  TEST_ASSERT_TRUE(s.ready());
  TEST_ASSERT_FALSE(s.faulted());
}

void test_contactor_opens_on_disengage() {
  kart::ContactorSequencer s;
  s.update(true, 0);
  s.update(true, 1000);
  TEST_ASSERT_TRUE(s.contactor_closed());
  s.update(false, 1100);
  TEST_ASSERT_FALSE(s.contactor_closed());
  TEST_ASSERT_FALSE(s.ready());
}

void test_contactor_bus_sense_ready_and_fault() {
  kart::ContactorConfig cfg;
  cfg.settle_ms = 500;
  cfg.has_bus_sense = true;
  kart::ContactorSequencer s(cfg);

  // Bus already charged: ready as soon as sense confirms, before settle.
  s.update(true, 0, /*bus_voltage_ok=*/true);
  s.update(true, 50, /*bus_voltage_ok=*/true);
  TEST_ASSERT_TRUE(s.ready());

  // Fresh sequencer, bus never comes up -> fault at settle timeout.
  kart::ContactorSequencer s2(cfg);
  s2.update(true, 0, /*bus_voltage_ok=*/false);
  s2.update(true, 600, /*bus_voltage_ok=*/false);
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
  RUN_TEST(test_contactor_closes_and_ready_after_settle);
  RUN_TEST(test_contactor_opens_on_disengage);
  RUN_TEST(test_contactor_bus_sense_ready_and_fault);
  return UNITY_END();
}
