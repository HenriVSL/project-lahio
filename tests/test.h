/* test.h - tiny test framework for the Mac headless tests (C++98, no libraries). Owner: T00.

   TEST(name) { ... CHECK(cond); CHECK_EQ(a, b); ... }   registers a test (static constructor).
   CHECK* record a failure with file:line and RETURN from the test function (no exceptions), so put
   cleanup before the checks or keep tests free of resources. Use test_log() for progress lines; write
   bigger dumps (world_dump, sim_dump_report, stat CSV) into files under tests/out/ (the runner's cwd is
   tests/). Tests run in registration order (per file: source order; files: link order). Each test
   must set up the state it needs (world_new, sim_new_city...) and must not rely on another test. */
#ifndef TEST_H
#define TEST_H
#include "common.h"

typedef void (*TestFn)(void);
struct TestReg { TestReg(const char *name, const char *file, TestFn fn); };
void test_fail(const char *file, int line, const char *expr);
void test_fail_eq(const char *file, int line, const char *expr, long long a, long long b);
void test_log(const char *fmt, ...);         /* indented line in the test output */

#define TEST(name) \
    static void test_fn_##name(void); \
    static TestReg test_reg_##name(#name, __FILE__, test_fn_##name); \
    static void test_fn_##name(void)

#define CHECK(cond) \
    do { if (!(cond)) { test_fail(__FILE__, __LINE__, #cond); return; } } while (0)
#define CHECK_EQ(a, b) \
    do { long long ca_ = (long long)(a), cb_ = (long long)(b); \
         if (ca_ != cb_) { test_fail_eq(__FILE__, __LINE__, #a " == " #b, ca_, cb_); return; } } while (0)

#endif
