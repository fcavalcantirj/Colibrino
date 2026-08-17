/*
 * Golden-trace runner shared by blink_dsp_golden and blink_code_golden.
 *
 * Fixture discovery uses traces/MANIFEST.sha256 as the index of promoted
 * files (a fixture is "promoted" only when it is listed): every listed
 * NAME.csv must have a listed NAME.labels.tsv. Each pair is streamed through
 * the blink pipeline and every labeled segment's expectation is checked
 * (impulse count for the dsp mode, click-candidate count for the code mode).
 *
 * Exit codes: 0 all expectations met; 1 failure (or oracle gate with no
 * fixtures); 77 skipped because no fixture pair is promoted (developer
 * preset only). Output never prints absolute paths, only fixture basenames.
 */
#ifndef COLIBRINO_V2_TEST_GOLDEN_RUNNER_H
#define COLIBRINO_V2_TEST_GOLDEN_RUNNER_H

typedef enum {
  GOLDEN_MODE_IMPULSES = 0, /* blink-dsp: IMPULSE events per segment */
  GOLDEN_MODE_CLICKS = 1    /* blink-code: CLICK_CANDIDATE per segment */
} golden_mode_t;

#define GOLDEN_EXIT_SKIP 77

int golden_main(golden_mode_t mode, const char *traces_dir, int oracle_gate);

#endif /* COLIBRINO_V2_TEST_GOLDEN_RUNNER_H */
