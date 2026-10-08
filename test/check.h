#pragma once
#include <cstdio>
#include <string>
#include <vector>

extern int g_checks, g_failures;

#define CHECK(c) do { g_checks++; if (!(c)) { g_failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { g_failures++; printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; std::string _a = (a), _b = (b); \
  if (_a != _b) { g_failures++; printf("FAIL %s:%d  %s == %s\n   got:  [%s]\n   want: [%s]\n", __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str()); } } while (0)

inline std::vector<void (*)()>& suites() { static std::vector<void (*)()> v; return v; }
struct SuiteReg { explicit SuiteReg(void (*f)()) { suites().push_back(f); } };
#define SUITE(fn) static void fn(); static SuiteReg reg_##fn(fn); static void fn()
