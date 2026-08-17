/*
 * blink-dsp golden oracle (rung 2): every labeled segment of every promoted
 * fixture pair under traces/ must produce exactly the expected number of
 * IMPULSE events (hard blinks: one per cue window; head sweeps above the
 * gate: zero; unconstrained segments are '*'). Skips (exit 77) with an
 * explicit message while no fixture is promoted; fails under the oracle
 * gate (COLIBRINO_V2_ORACLE=ON).
 */
#include "support/golden_runner.h"

int main(void) {
  return golden_main(GOLDEN_MODE_IMPULSES, CV2_TRACES_DIR, CV2_ORACLE);
}
