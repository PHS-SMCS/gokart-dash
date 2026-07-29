// Host-side tests for the Steervo control core safety properties.
#include <unity.h>

#include "steer_controller.h"

using kart::SteerSet;
using kart::SteerState;
using steervo::PotCalibration;
using steervo::SteerConfig;
using steervo::SteerController;

namespace {

// Symmetric calibration: pot 1024..3072 maps to -30.00deg..+30.00deg.
PotCalibration test_cal() {
  PotCalibration cal{};
  cal.raw_center = 2048;
  cal.raw_left = 1024;
  cal.raw_right = 3072;
  cal.angle_left_cdeg = -3000;
  cal.angle_right_cdeg = 3000;
  cal.valid = true;
  return cal;
}

SteerController make_ready(uint32_t t0 = 100) {
  SteerController c;
  c.set_calibration(test_cal());
  c.tick(t0, 2048);  // first plausible pot -> READY
  return c;
}

void feed_setpoint(SteerController &c, int16_t cdeg, uint32_t now,
                   uint8_t seq = 0, bool enable = true) {
  SteerSet s{enable, cdeg, seq};
  c.on_steer_set(s, now);
}

}  // namespace

void setUp() {}
void tearDown() {}

// -------------------- activation gating --------------------

void test_init_to_ready_on_plausible_pot() {
  SteerController c;
  c.set_calibration(test_cal());
  TEST_ASSERT_EQUAL((int)SteerState::kInit, (int)c.state());
  c.tick(100, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
}

void test_no_output_without_setpoints() {
  SteerController c = make_ready();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(110, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
}

void test_no_output_when_uncalibrated() {
  SteerController c;  // no calibration
  c.tick(100, 2048);
  feed_setpoint(c, 1000, 105);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(110, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultNotCalibrated);
}

void test_active_drives_toward_setpoint() {
  SteerController c = make_ready();
  feed_setpoint(c, 1500, 105);  // want +15 deg, currently 0
  float out = c.tick(110, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_TRUE(out > 0.0f);  // pushes right

  feed_setpoint(c, -1500, 115);
  out = c.tick(120, 2048);
  TEST_ASSERT_TRUE(out < 0.0f);  // pushes left
}

void test_enable_false_means_no_output() {
  SteerController c = make_ready();
  feed_setpoint(c, 1500, 105, 0, /*enable=*/false);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(110, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
}

// -------------------- staleness --------------------

void test_stale_setpoints_deenergize_motor() {
  SteerController c = make_ready();
  feed_setpoint(c, 1500, 105);
  TEST_ASSERT_TRUE(c.tick(110, 2048) > 0.0f);

  // No frames for >150 ms.
  uint32_t later = 110 + kart::kSteerSetTimeoutMs + 10;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(later, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultSetpointStale);
}

void test_fresh_frames_recover_from_staleness() {
  SteerController c = make_ready();
  feed_setpoint(c, 1500, 105);
  c.tick(110, 2048);
  uint32_t later = 110 + kart::kSteerSetTimeoutMs + 10;
  c.tick(later, 2048);
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultSetpointStale);

  feed_setpoint(c, 1000, later + 5);
  float out = c.tick(later + 10, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_TRUE(out > 0.0f);
  TEST_ASSERT_FALSE(c.fault_bits() & kart::kSteerFaultSetpointStale);
}

// -------------------- pot plausibility --------------------

void test_pot_out_of_range_is_latched_hard_fault() {
  SteerController c = make_ready();
  feed_setpoint(c, 1500, 105);
  c.tick(110, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());

  // Wiper falls off the track: rail reading.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(120, 4095));
  TEST_ASSERT_EQUAL((int)SteerState::kFault, (int)c.state());
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultPotRange);

  // Plausible readings and fresh setpoints do NOT clear a hard fault.
  feed_setpoint(c, 0, 130);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(140, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kFault, (int)c.state());
}

// -------------------- soft limits --------------------

void test_setpoint_clamped_to_soft_limits() {
  SteerConfig cfg;
  cfg.kp = 0.002f;
  cfg.soft_limit_margin_cdeg = 100;
  SteerController c(cfg);
  c.set_calibration(test_cal());
  // Pot at the calibrated right edge (= +3000 cdeg).
  c.tick(100, 3072);
  // Demand far beyond the calibrated range.
  feed_setpoint(c, 10000, 105);
  float out = c.tick(110, 3072);
  // Clamped target is 2900; measured is 3000 -> error negative, motor must
  // pull BACK toward the range, never push further out.
  TEST_ASSERT_TRUE(out < 0.0f);
}

// -------------------- stall detection --------------------

void test_stall_faults_after_sustained_saturated_output() {
  SteerConfig cfg;
  cfg.kp = 1.0f;  // saturates instantly on any real error
  cfg.stall_timeout_ms = 800;
  SteerController c(cfg);
  c.set_calibration(test_cal());
  c.tick(0, 2048);

  // Mechanically blocked: pot never moves while we push hard.
  uint32_t t = 10;
  bool faulted = false;
  for (; t < 2000; t += 10) {
    feed_setpoint(c, 2000, t);  // keep frames fresh
    c.tick(t, 2048);            // pot frozen at center
    if (c.state() == SteerState::kFault) {
      faulted = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(faulted);
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultStall);
  TEST_ASSERT_TRUE(t >= cfg.stall_timeout_ms);  // not before the window
}

void test_movement_resets_stall_window() {
  SteerConfig cfg;
  cfg.kp = 1.0f;
  cfg.stall_timeout_ms = 800;
  SteerController c(cfg);
  c.set_calibration(test_cal());
  c.tick(0, 2048);

  // Pot creeps along as commanded: no stall even while saturated.
  uint16_t pot = 2048;
  for (uint32_t t = 10; t < 3000; t += 10) {
    feed_setpoint(c, 2900, t);
    c.tick(t, pot);
    if (t % 100 == 0 && pot < 3000) {
      pot += 20;  // ~0.6 deg every 100 ms
    }
    TEST_ASSERT_NOT_EQUAL((int)SteerState::kFault, (int)c.state());
  }
}

void test_runaway_wrong_way_faults() {
  // Motor pushes (saturated) but the pot moves AWAY from the target: an
  // inverted-sign runaway. The convergence watchdog must fault even though the
  // pot IS moving (the old movement-only stall check would have missed this).
  SteerConfig cfg;
  cfg.kp = 1.0f;  // saturate on any error
  cfg.stall_timeout_ms = 800;
  SteerController c(cfg);
  c.set_calibration(test_cal());
  c.tick(0, 2048);

  uint16_t pot = 2048;  // 0 deg
  bool faulted = false;
  uint32_t t = 10;
  for (; t < 2000; t += 10) {
    feed_setpoint(c, 1000, t);  // want +10 deg (right of center)
    c.tick(t, pot);
    // Runaway: drifts left (away from the +target), staying inside the pot
    // range so this is the watchdog firing, not over-travel / pot-range.
    if (t % 50 == 0 && pot > 1600) pot -= 10;
    if (c.state() == SteerState::kFault) {
      faulted = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(faulted);
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultStall);
  TEST_ASSERT_TRUE(t >= cfg.stall_timeout_ms);
}

void test_fast_wiggle_does_not_fault() {
  // Rapidly reversing setpoint with the pot chasing (lagging) must NOT trip the
  // watchdog: the pot is being driven correctly, it just never catches the racing
  // target. This is the reported bug — the old error-based watchdog faulted here.
  SteerConfig cfg;
  cfg.kp = 1.0f;  // saturates on any real error -> always "pushing"
  cfg.stall_timeout_ms = 800;
  SteerController c(cfg);
  c.set_calibration(test_cal());
  c.tick(0, 2048);

  int pot = 2048;      // centered
  bool right = true;
  for (uint32_t t = 10; t < 4000; t += 10) {
    if (t % 60 == 0) right = !right;             // flip the target every 60 ms
    int target_raw = right ? 3072 : 1024;        // full right / full left
    feed_setpoint(c, right ? 3000 : -3000, t);
    c.tick(t, (uint16_t)pot);
    // Pot chases the current target at a limited rate (lags the fast flips).
    int step = 24;
    if (pot < target_raw) pot = pot + step < target_raw ? pot + step : target_raw;
    else if (pot > target_raw) pot = pot - step > target_raw ? pot - step : target_raw;
    TEST_ASSERT_NOT_EQUAL((int)SteerState::kFault, (int)c.state());
  }
}

void test_stall_fault_auto_recovers() {
  // A stall must self-clear after the cooldown so the steering never stays
  // latched off (which used to need an ESP32 reset).
  SteerConfig cfg;
  cfg.kp = 1.0f;
  cfg.stall_timeout_ms = 800;
  cfg.stall_recover_ms = 1000;
  SteerController c(cfg);
  c.set_calibration(test_cal());
  c.tick(0, 2048);

  // Jam: pot frozen while pushing -> stall fault.
  uint32_t t = 10;
  for (; t < 2000; t += 10) {
    feed_setpoint(c, 2000, t);
    c.tick(t, 2048);
    if (c.state() == SteerState::kFault) break;
  }
  TEST_ASSERT_EQUAL((int)SteerState::kFault, (int)c.state());
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultStall);
  uint32_t fault_t = t;

  // Still latched before the cooldown elapses.
  feed_setpoint(c, 2000, fault_t + 500);
  c.tick(fault_t + 500, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kFault, (int)c.state());

  // After the cooldown, with the jam released (pot free, setpoint centred),
  // control resumes automatically — no reset.
  uint32_t rec = fault_t + cfg.stall_recover_ms + 20;
  feed_setpoint(c, 0, rec);
  c.tick(rec, 2048);
  TEST_ASSERT_NOT_EQUAL((int)SteerState::kFault, (int)c.state());
  TEST_ASSERT_FALSE(c.fault_bits() & kart::kSteerFaultStall);
}

// -------------------- over-travel (pot protection) --------------------

void test_over_travel_while_active_recovers_not_latched() {
  // Overshooting a stop while ACTIVE must NOT latch a hard fault (the old
  // behavior stranded the steering at full lock, unable to return to centre).
  // It stays active, refuses to drive deeper, and recovers toward centre.
  SteerController c = make_ready();
  feed_setpoint(c, 1500, 105);
  c.tick(110, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());

  // Pot driven past the right stop (raw_right=3072, margin 80 -> >3152), still a
  // plausible ADC reading (not a rail). Commanded hard right: the clamp forbids
  // driving deeper, but this is not a fault and the motor stays live.
  feed_setpoint(c, 3000, 118);
  float out = c.tick(120, 3200);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_FALSE(c.fault_bits() & kart::kSteerFaultOverTravel);  // never a fault
  TEST_ASSERT_TRUE(out <= 0.0f);  // cannot drive deeper into the right stop

  // Commanding centre while still past the stop drives back OUT (toward centre).
  feed_setpoint(c, 0, 130);
  TEST_ASSERT_TRUE(c.tick(140, 3200) < 0.0f);

  // Once back in range, normal operation continues (nothing was latched).
  feed_setpoint(c, 1500, 150, 1);
  float out3 = c.tick(160, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_TRUE(out3 > 0.0f);
}

void test_over_travel_idle_recovers_toward_center_not_further() {
  SteerController c = make_ready();  // READY, motor never driven
  // Pot sitting past the RIGHT stop while idle (raw 3200 > 3072+80). Commanding
  // centre must drive it back OUT (toward centre), not lock up, and not latch.
  feed_setpoint(c, 0, 105);
  float out = c.tick(110, 3200);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());  // allowed to run
  TEST_ASSERT_FALSE(c.fault_bits() & kart::kSteerFaultOverTravel);  // not latched (was idle)
  TEST_ASSERT_TRUE(out < 0.0f);  // negative == toward centre, out of the right stop

  // Even commanded HARD RIGHT while past the right stop, the clamp forbids
  // driving deeper into the stop: output can never be positive here.
  feed_setpoint(c, 3000, 118);
  float out2 = c.tick(122, 3200);
  TEST_ASSERT_TRUE(out2 <= 0.0f);

  // Back inside range: normal operation resumes (no latch ever held it down).
  feed_setpoint(c, 1500, 130, 1);
  float out3 = c.tick(135, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_TRUE(out3 > 0.0f);
}

// Past the LEFT stop, the mirror image: recovery is positive (toward centre),
// and a hard-left command cannot drive further into the left stop.
void test_over_travel_left_recovers_positive() {
  SteerController c = make_ready();
  feed_setpoint(c, 0, 105);
  float out = c.tick(110, 900);  // raw 900 < 1024-80 == past the left stop
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_TRUE(out > 0.0f);  // positive == toward centre, out of the left stop

  feed_setpoint(c, -3000, 118);  // hard left
  float out2 = c.tick(122, 900);
  TEST_ASSERT_TRUE(out2 >= 0.0f);  // cannot drive deeper into the left stop
}

// -------------------- status reporting --------------------

void test_status_reflects_state_and_seq_echo() {
  SteerController c = make_ready();
  feed_setpoint(c, 500, 105, /*seq=*/77);
  c.tick(110, 2048);

  kart::SteerStatus st = c.status();
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)st.state);
  TEST_ASSERT_EQUAL(77, st.seq_echo);
  TEST_ASSERT_EQUAL(2048, st.pot_raw);
  TEST_ASSERT_EQUAL(0, st.measured_cdeg);
}

// -------------------- guided calibration --------------------

void test_calibration_sequence_commits_and_enables() {
  SteerController c;  // starts uncalibrated
  c.tick(100, 2048);
  TEST_ASSERT_TRUE(c.fault_bits() & kart::kSteerFaultNotCalibrated);

  TEST_ASSERT_FALSE(c.on_cal(kart::SteerCalCmd::kEnter, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kCalibrating, (int)c.state());
  // No motor output while calibrating, even with fresh enabled setpoints.
  feed_setpoint(c, 1500, 105);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.tick(110, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kCalibrating, (int)c.state());

  c.on_cal(kart::SteerCalCmd::kMarkCenter, 2050);
  c.on_cal(kart::SteerCalCmd::kMarkLeft, 1000);
  c.on_cal(kart::SteerCalCmd::kMarkRight, 3100);
  // Commit returns true (caller persists to NVS) and leaves us READY+calibrated.
  TEST_ASSERT_TRUE(c.on_cal(kart::SteerCalCmd::kSaveExit, 2050));
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
  TEST_ASSERT_FALSE(c.fault_bits() & kart::kSteerFaultNotCalibrated);

  const PotCalibration &cal = c.calibration();
  TEST_ASSERT_EQUAL(2050, cal.raw_center);
  TEST_ASSERT_EQUAL(1000, cal.raw_left);
  TEST_ASSERT_EQUAL(3100, cal.raw_right);
  TEST_ASSERT_EQUAL((int)-kart::kSteerRangeCdeg, cal.angle_left_cdeg);
  TEST_ASSERT_EQUAL((int)kart::kSteerRangeCdeg, cal.angle_right_cdeg);

  // Now a fresh enabled setpoint should activate the motor.
  feed_setpoint(c, 1000, 120, 1);
  float out = c.tick(125, 2050);
  TEST_ASSERT_EQUAL((int)SteerState::kActive, (int)c.state());
  TEST_ASSERT_TRUE(out > 0.0f);
}

void test_incomplete_calibration_does_not_commit() {
  SteerController c;
  c.tick(100, 2048);
  c.on_cal(kart::SteerCalCmd::kEnter, 2048);
  c.on_cal(kart::SteerCalCmd::kMarkCenter, 2048);
  // Missing left/right marks: SAVE_EXIT must not commit, stays calibrating.
  TEST_ASSERT_FALSE(c.on_cal(kart::SteerCalCmd::kSaveExit, 2048));
  TEST_ASSERT_EQUAL((int)SteerState::kCalibrating, (int)c.state());
  TEST_ASSERT_FALSE(c.calibration().valid);
}

void test_calibration_abort_restores_prior_cal() {
  SteerController c = make_ready();  // already calibrated + READY
  c.on_cal(kart::SteerCalCmd::kEnter, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kCalibrating, (int)c.state());
  c.on_cal(kart::SteerCalCmd::kAbort, 2048);
  TEST_ASSERT_EQUAL((int)SteerState::kReady, (int)c.state());
  TEST_ASSERT_TRUE(c.calibration().valid);  // prior calibration intact
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_init_to_ready_on_plausible_pot);
  RUN_TEST(test_no_output_without_setpoints);
  RUN_TEST(test_no_output_when_uncalibrated);
  RUN_TEST(test_active_drives_toward_setpoint);
  RUN_TEST(test_enable_false_means_no_output);
  RUN_TEST(test_stale_setpoints_deenergize_motor);
  RUN_TEST(test_fresh_frames_recover_from_staleness);
  RUN_TEST(test_pot_out_of_range_is_latched_hard_fault);
  RUN_TEST(test_setpoint_clamped_to_soft_limits);
  RUN_TEST(test_stall_faults_after_sustained_saturated_output);
  RUN_TEST(test_movement_resets_stall_window);
  RUN_TEST(test_runaway_wrong_way_faults);
  RUN_TEST(test_fast_wiggle_does_not_fault);
  RUN_TEST(test_stall_fault_auto_recovers);
  RUN_TEST(test_over_travel_while_active_recovers_not_latched);
  RUN_TEST(test_over_travel_idle_recovers_toward_center_not_further);
  RUN_TEST(test_over_travel_left_recovers_positive);
  RUN_TEST(test_status_reflects_state_and_seq_echo);
  RUN_TEST(test_calibration_sequence_commits_and_enables);
  RUN_TEST(test_incomplete_calibration_does_not_commit);
  RUN_TEST(test_calibration_abort_restores_prior_cal);
  return UNITY_END();
}
