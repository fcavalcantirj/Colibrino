/*
 * blink-code synthetic oracle (rung 3 + 4): the temporal code
 * double (300-700) . pause (800-1400) . double, its restart rules, the
 * click refractory, and the emitted event fields. Inputs are hand-built
 * blink events; every case asserts the click count and the sequence state.
 */
#include <string.h>

#include "colibrino/v2/blink_code.h"
#include "support/blink_fixture.h"
#include "support/unity_main.h"

typedef struct {
  cv2_blink_code_state_t st;
  cv2_blink_code_config_t cfg;
  uint32_t seq;
  uint32_t clicks;
  cv2_gesture_event_t last_click;
} code_fixture_t;

static void code_init(code_fixture_t *f) {
  memset(f, 0, sizeof *f);
  cv2_blink_code_config_defaults(&f->cfg);
  cv2_blink_code_reset(&f->st);
}

static bool impulse(code_fixture_t *f, cv2_ms_t t_ms) {
  const cv2_blink_event_t ev = blink_impulse_at(++f->seq, t_ms, 45u);
  const cv2_gesture_event_t g = cv2_blink_code_step(&f->st, &ev, &f->cfg);
  if (g.hdr.kind == (uint8_t)CV2_GESTURE_CLICK_CANDIDATE) {
    ++f->clicks;
    f->last_click = g;
    return true;
  }
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, g.hdr.kind);
  return false;
}

static void cancel(code_fixture_t *f, cv2_ms_t t_ms) {
  const cv2_blink_event_t ev =
      blink_cancel_at(++f->seq, t_ms, CV2_BLINK_REASON_HEAD_MOTION);
  const cv2_gesture_event_t g = cv2_blink_code_step(&f->st, &ev, &f->cfg);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE, g.hdr.kind);
}

/* Runs a four-impulse pattern with the given gaps; returns whether the
 * fourth impulse clicked. */
static bool pattern(uint32_t gap1, uint32_t gap2, uint32_t gap3) {
  code_fixture_t f;
  code_init(&f);
  uint32_t t = 1000u;
  TEST_ASSERT_FALSE(impulse(&f, t));
  t += gap1;
  TEST_ASSERT_FALSE(impulse(&f, t));
  t += gap2;
  TEST_ASSERT_FALSE(impulse(&f, t));
  t += gap3;
  return impulse(&f, t);
}

static void test_double_pause_double_clicks_at_fourth(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_EQUAL_UINT8(2u, f.st.sequence_impulses);
  TEST_ASSERT_FALSE(impulse(&f, 2300u));
  TEST_ASSERT_EQUAL_UINT8(3u, f.st.sequence_impulses);
  TEST_ASSERT_TRUE(impulse(&f, 2700u));
  TEST_ASSERT_EQUAL_UINT32(1u, f.clicks);
  TEST_ASSERT_EQUAL_UINT32(1u, f.st.completed_sequences);
  TEST_ASSERT_EQUAL_UINT8(0u, f.st.sequence_impulses);
  TEST_ASSERT_TRUE(f.st.click_suppressed);
  TEST_ASSERT_EQUAL_UINT32(2700u, f.st.last_click_ms);
}

static void test_uniform_four_never_clicks(void) {
  code_fixture_t f;
  code_init(&f);
  uint32_t t = 1000u;
  for (int k = 0; k < 8; ++k) {
    TEST_ASSERT_FALSE(impulse(&f, t));
    t += 400u;
  }
  TEST_ASSERT_EQUAL_UINT32(0u, f.clicks);
  /* Same for a uniform cadence inside the pause window. */
  code_init(&f);
  t = 1000u;
  for (int k = 0; k < 8; ++k) {
    TEST_ASSERT_FALSE(impulse(&f, t));
    t += 1000u;
  }
  TEST_ASSERT_EQUAL_UINT32(0u, f.clicks);
}

static void test_three_impulses_never_click(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_FALSE(impulse(&f, 2300u));
  TEST_ASSERT_EQUAL_UINT32(0u, f.clicks);
  TEST_ASSERT_EQUAL_UINT8(3u, f.st.sequence_impulses);
  /* Then a long silence and a lone impulse: restart, no click. */
  TEST_ASSERT_FALSE(impulse(&f, 9000u));
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  TEST_ASSERT_EQUAL_UINT32(0u, f.clicks);
}

static void test_single_never_clicks(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 5000u));
  TEST_ASSERT_FALSE(impulse(&f, 9000u));
  TEST_ASSERT_EQUAL_UINT32(0u, f.clicks);
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
}

static void test_shifted_pair_retained_as_first_double(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u)); /* double */
  TEST_ASSERT_FALSE(impulse(&f, 1800u)); /* pause expected, short gap */
  TEST_ASSERT_EQUAL_UINT8(2u, f.st.sequence_impulses);
  TEST_ASSERT_EQUAL_UINT32(1400u, f.st.first_impulse_ms);
  TEST_ASSERT_FALSE(impulse(&f, 2700u)); /* pause 900 */
  TEST_ASSERT_TRUE(impulse(&f, 3100u));  /* double */
  TEST_ASSERT_EQUAL_UINT16(1700u, f.last_click.span_ms);
  /* Any other mismatch makes the current impulse the pattern start. */
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_FALSE(impulse(&f, 3400u)); /* 2000: neither window */
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  TEST_ASSERT_EQUAL_UINT32(3400u, f.st.first_impulse_ms);
}

static void test_cancel_resets_partial_sequence(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  cancel(&f, 1900u);
  TEST_ASSERT_EQUAL_UINT8(0u, f.st.sequence_impulses);
  TEST_ASSERT_FALSE(impulse(&f, 2300u)); /* would have been the pause */
  TEST_ASSERT_FALSE(impulse(&f, 2700u));
  TEST_ASSERT_EQUAL_UINT32(0u, f.clicks);
  TEST_ASSERT_EQUAL_UINT8(2u, f.st.sequence_impulses);
  /* CANCEL never touches the click refractory or the counters. */
  TEST_ASSERT_EQUAL_UINT32(0u, f.st.completed_sequences);
}

static void test_click_refractory_ignores_impulses(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_FALSE(impulse(&f, 2300u));
  TEST_ASSERT_TRUE(impulse(&f, 2700u));
  TEST_ASSERT_TRUE(cv2_blink_code_in_refractory(&f.st, &f.cfg, 2700u));
  TEST_ASSERT_TRUE(cv2_blink_code_in_refractory(&f.st, &f.cfg, 4199u));
  TEST_ASSERT_FALSE(cv2_blink_code_in_refractory(&f.st, &f.cfg, 4200u));
  /* Impulses inside 1500 ms after the click do not start a pattern. */
  TEST_ASSERT_FALSE(impulse(&f, 2800u));
  TEST_ASSERT_FALSE(impulse(&f, 3200u));
  TEST_ASSERT_FALSE(impulse(&f, 4100u));
  TEST_ASSERT_FALSE(impulse(&f, 4199u));
  TEST_ASSERT_EQUAL_UINT8(0u, f.st.sequence_impulses);
  /* Exactly 1500 ms after the click the first impulse counts again. */
  TEST_ASSERT_FALSE(impulse(&f, 4200u));
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  TEST_ASSERT_FALSE(f.st.click_suppressed);
  TEST_ASSERT_EQUAL_UINT32(1u, f.clicks);
}

static void test_second_pattern_after_refractory_clicks_again(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_FALSE(impulse(&f, 2300u));
  TEST_ASSERT_TRUE(impulse(&f, 2700u));
  TEST_ASSERT_FALSE(impulse(&f, 4300u));
  TEST_ASSERT_FALSE(impulse(&f, 4700u));
  TEST_ASSERT_FALSE(impulse(&f, 5600u));
  TEST_ASSERT_TRUE(impulse(&f, 6000u));
  TEST_ASSERT_EQUAL_UINT32(2u, f.clicks);
  TEST_ASSERT_EQUAL_UINT32(2u, f.st.completed_sequences);
  TEST_ASSERT_EQUAL_UINT32(2u, f.last_click.hdr.seq);
}

static void test_window_boundaries_inclusive(void) {
  /* double window [300, 700], pause window [800, 1400]. */
  TEST_ASSERT_TRUE(pattern(300u, 800u, 300u));
  TEST_ASSERT_TRUE(pattern(700u, 1400u, 700u));
  TEST_ASSERT_TRUE(pattern(300u, 1400u, 700u));
  TEST_ASSERT_FALSE(pattern(299u, 800u, 300u));
  TEST_ASSERT_FALSE(pattern(701u, 800u, 300u));
  TEST_ASSERT_FALSE(pattern(300u, 799u, 300u));
  TEST_ASSERT_FALSE(pattern(300u, 1401u, 300u));
  TEST_ASSERT_FALSE(pattern(300u, 800u, 299u));
  TEST_ASSERT_FALSE(pattern(300u, 800u, 701u));
}

static void test_event_fields(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_FALSE(impulse(&f, 2300u));
  TEST_ASSERT_TRUE(impulse(&f, 2700u));
  const cv2_gesture_event_t g = f.last_click;
  TEST_ASSERT_EQUAL_HEX16(CV2_EVENT_MAGIC, g.hdr.magic);
  TEST_ASSERT_EQUAL_UINT8(CV2_EVENT_VERSION, g.hdr.version);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_CLICK_CANDIDATE, g.hdr.kind);
  TEST_ASSERT_EQUAL_UINT16(CV2_PRODUCER_BLINK_CODE, g.hdr.producer_id);
  TEST_ASSERT_EQUAL_UINT16(CV2_EVENT_SIZE, g.hdr.size);
  TEST_ASSERT_EQUAL_UINT32(1u, g.hdr.seq);
  TEST_ASSERT_EQUAL_UINT32(2700u, g.hdr.t_ms);
  TEST_ASSERT_EQUAL_UINT16(CV2_FEEL_TTL_CLICK_MS, g.ttl_ms);
  TEST_ASSERT_EQUAL_UINT8(CV2_FEEL_BLINK_CODE_CONFIDENCE, g.confidence);
  TEST_ASSERT_EQUAL_UINT8(4u, g.impulses);
  TEST_ASSERT_EQUAL_UINT16(1700u, g.span_ms);
  TEST_ASSERT_EQUAL_INT(CV2_HDR_OK,
                        cv2_header_validate(&g.hdr, CV2_EVENT_SIZE,
                                            CV2_GESTURE_KIND_MAX, g.ttl_ms));
  /* NONE outputs are all-zero. */
  const cv2_blink_event_t none_in = blink_impulse_at(99u, 2705u, 45u);
  cv2_blink_event_t none_copy = none_in;
  none_copy.hdr.kind = CV2_BLINK_NONE;
  const cv2_gesture_event_t g0 = cv2_blink_code_step(&f.st, &none_copy, &f.cfg);
  cv2_gesture_event_t zero;
  memset(&zero, 0, sizeof zero);
  TEST_ASSERT_EQUAL_MEMORY(&zero, &g0, sizeof zero);
}

static void test_malformed_input_never_advances(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  cv2_blink_event_t bad = blink_impulse_at(50u, 1400u, 45u);
  bad.hdr.magic = 0x1111u;
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE,
                          cv2_blink_code_step(&f.st, &bad, &f.cfg).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  bad = blink_impulse_at(51u, 1400u, 45u);
  bad.hdr.producer_id = CV2_PRODUCER_COUNT;
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE,
                          cv2_blink_code_step(&f.st, &bad, &f.cfg).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  bad = blink_impulse_at(52u, 1400u, 45u);
  bad.hdr.size = 16u;
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE,
                          cv2_blink_code_step(&f.st, &bad, &f.cfg).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  /* A well-formed 24-byte event from a known but non-blink producer (for
   * example a mis-routed gesture or voice event whose kind value collides
   * with IMPULSE) must not advance the pattern either. */
  bad = blink_impulse_at(53u, 1400u, 45u);
  bad.hdr.producer_id = (uint16_t)CV2_PRODUCER_BLINK_CODE;
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE,
                          cv2_blink_code_step(&f.st, &bad, &f.cfg).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  bad = blink_impulse_at(54u, 1400u, 45u);
  bad.hdr.producer_id = (uint16_t)CV2_PRODUCER_VOICE;
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE,
                          cv2_blink_code_step(&f.st, &bad, &f.cfg).hdr.kind);
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
  TEST_ASSERT_EQUAL_UINT8(CV2_GESTURE_NONE,
                          cv2_blink_code_step(&f.st, NULL, &f.cfg).hdr.kind);
  /* The well-formed continuation still works. */
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_EQUAL_UINT8(2u, f.st.sequence_impulses);
}

static void test_reset_clears_everything(void) {
  code_fixture_t f;
  code_init(&f);
  TEST_ASSERT_FALSE(impulse(&f, 1000u));
  TEST_ASSERT_FALSE(impulse(&f, 1400u));
  TEST_ASSERT_FALSE(impulse(&f, 2300u));
  TEST_ASSERT_TRUE(impulse(&f, 2700u));
  cv2_blink_code_reset(&f.st);
  TEST_ASSERT_EQUAL_UINT8(0u, f.st.sequence_impulses);
  TEST_ASSERT_FALSE(f.st.click_suppressed);
  TEST_ASSERT_EQUAL_UINT32(0u, f.st.completed_sequences);
  TEST_ASSERT_EQUAL_UINT32(1u, f.st.next_seq);
  /* Right after reset an impulse counts (no stale refractory). */
  TEST_ASSERT_FALSE(impulse(&f, 2800u));
  TEST_ASSERT_EQUAL_UINT8(1u, f.st.sequence_impulses);
}

CV2_UNITY_MAIN(
  RUN_TEST(test_double_pause_double_clicks_at_fourth);
  RUN_TEST(test_uniform_four_never_clicks);
  RUN_TEST(test_three_impulses_never_click);
  RUN_TEST(test_single_never_clicks);
  RUN_TEST(test_shifted_pair_retained_as_first_double);
  RUN_TEST(test_cancel_resets_partial_sequence);
  RUN_TEST(test_click_refractory_ignores_impulses);
  RUN_TEST(test_second_pattern_after_refractory_clicks_again);
  RUN_TEST(test_window_boundaries_inclusive);
  RUN_TEST(test_event_fields);
  RUN_TEST(test_malformed_input_never_advances);
  RUN_TEST(test_reset_clears_everything);
)
