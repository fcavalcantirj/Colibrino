/*
 * blink-code golden oracle (rung 2): every labeled segment of every promoted
 * fixture pair must produce exactly the expected number of CLICK_CANDIDATE
 * events (coded groups: one each; hard/soft blinks, rest, head sweeps,
 * confounders and evenly spaced four-blink groups: zero). Skips (exit 77)
 * while no fixture is promoted; fails under the oracle gate.
 */
#include "support/golden_runner.h"

int main(void) {
  return golden_main(GOLDEN_MODE_CLICKS, CV2_TRACES_DIR, CV2_ORACLE);
}
