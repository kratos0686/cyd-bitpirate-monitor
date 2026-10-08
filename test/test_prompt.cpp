#include "check.h"
#include "prompt.h"

SUITE(prompt_tests) {
  char m[16];
  CHECK(parsePromptMode("HIZ>", m, sizeof m));       CHECK_STR(m, "HIZ");
  CHECK(parsePromptMode("HIZ> help", m, sizeof m));  CHECK_STR(m, "HIZ");
  CHECK(parsePromptMode("I2C> ", m, sizeof m));      CHECK_STR(m, "I2C");
  CHECK(!parsePromptMode("HIZ>foo", m, sizeof m));
  CHECK(!parsePromptMode(">", m, sizeof m));
  CHECK(!parsePromptMode("", m, sizeof m));
  CHECK(!parsePromptMode("Enter index (default 1): ", m, sizeof m));
  CHECK(!parsePromptMode("=== Help Shell ===", m, sizeof m));
  CHECK(!parsePromptMode("a b> ", m, sizeof m));
  CHECK(!parsePromptMode("VERYLONGMODENAMEXX> ", m, sizeof m));
  char tiny[3];
  CHECK(!parsePromptMode("HIZ>", tiny, sizeof tiny));
}
