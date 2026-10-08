#include "check.h"
int g_checks = 0, g_failures = 0;
int main() {
  for (auto f : suites()) f();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
