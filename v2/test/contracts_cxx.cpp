// Every public header must be consumable from C++17 (the sticks3 firmware is
// C++17); the extern "C" guards and the absence of C-only syntax are the
// contract. One call proves the symbols link with C linkage.
#include <cstddef>

#include "colibrino/v2/access_intent.h"
#include "colibrino/v2/blink_code.h"
#include "colibrino/v2/blink_dsp.h"
#include "colibrino/v2/common.h"
#include "colibrino/v2/feel_defaults.h"
#include "colibrino/v2/imu_motion.h"
#include "colibrino/v2/profile.h"
#include "colibrino/v2/wire.h"
#include "support/unity_main.h"

namespace {

void test_headers_compile_and_link_from_cxx() {
  static_assert(sizeof(cv2_event_hdr_t) == 16, "hdr");
  static_assert(sizeof(cv2_intent_event_t) == 24, "intent");
  cv2_profile_t p{};
  cv2_profile_defaults(&p);
  TEST_ASSERT_EQUAL_INT(CV2_PROFILE_OK, cv2_profile_validate(&p, sizeof p));
  cv2_intent_state_t st{};
  cv2_intent_init(&st);
  cv2_intent_config_t cfg{};
  cv2_intent_config_defaults(&cfg);
  cv2_intent_context_t ctx{};
  const cv2_action_t a = cv2_intent_arbitrate(&st, &ctx, nullptr, 0u, &cfg);
  TEST_ASSERT_EQUAL_UINT8(CV2_INTENT_NONE, a.kind);
  TEST_ASSERT_EQUAL_UINT8(1u, a.release_all);
  TEST_ASSERT_EQUAL_UINT8(CV2_FAULT_DISCONNECTED, a.fault);
}

}  // namespace

CV2_UNITY_MAIN(RUN_TEST(test_headers_compile_and_link_from_cxx);)
