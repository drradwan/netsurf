/*
 * test_qjscore.c — TDD smoke test for qjscore.c (Phase 2)
 *
 * Standalone test: stubs out NetSurf-specific macros/types so we can
 * compile and run qjscore.c against a host-native QuickJS without the
 * full NetSurf build system.
 *
 * Build:
 *   gcc -I. -I../../../.. -DQJSCORE_TEST_BUILD \
 *       -I<qjs_include> test_qjscore.c qjscore.c <libquickjs.a> -lm -o test_qjscore
 *
 * (See Makefile in this directory)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/* ── NetSurf stub types ──────────────────────────────────────────────────── */
typedef enum {
	NSERROR_OK = 0,
	NSERROR_NOMEM,
	NSERROR_INIT_FAILED,
	NSERROR_NOT_IMPLEMENTED,
} nserror;

/* Forward-declare dom types as opaque (Phase 2 stubs never dereference them) */
typedef struct dom_event     dom_event;
typedef struct dom_document  dom_document;
typedef struct dom_node      dom_node;
typedef struct dom_element   dom_element;
typedef struct dom_string    dom_string;

#define NSLOG(cat, level, fmt, ...) \
	fprintf(stderr, "[" #cat "|" #level "] " fmt "\n", ##__VA_ARGS__)

static inline void javascript_init(void) {}
static inline void qjsw_reset_generics_state(void) {}

/* Pull in the implementation under test */
#include "qjscore.c"

/* ── Test helpers ────────────────────────────────────────────────────────── */
static int tests_run    = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
		tests_failed++; \
	} else { \
		fprintf(stderr, "PASS %s\n", msg); \
	} \
} while (0)

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_heap_create_destroy(void)
{
	jsheap *heap = NULL;
	nserror err = js_newheap(10, &heap);
	CHECK(err == NSERROR_OK,         "js_newheap returns NSERROR_OK");
	CHECK(heap != NULL,              "js_newheap sets heap pointer");
	js_destroyheap(heap);
	CHECK(1,                         "js_destroyheap does not crash");
}

static void test_thread_create_destroy(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;
	nserror err;

	err = js_newheap(10, &heap);
	CHECK(err == NSERROR_OK, "heap created for thread test");

	err = js_newthread(heap, (void *)0xDEAD, (void *)0xBEEF, &thread);
	CHECK(err == NSERROR_OK,  "js_newthread returns NSERROR_OK");
	CHECK(thread != NULL,     "js_newthread sets thread pointer");

	js_destroythread(thread);
	js_destroyheap(heap);
	CHECK(1, "thread+heap destroyed without crash");
}

static void test_exec_trivial(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Trivial expression — should execute without error */
	const uint8_t script[] = "1 + 1";
	bool ok = js_exec(thread, script, sizeof(script) - 1, "test_trivial");
	CHECK(ok, "js_exec trivial script returns true");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_exec_returns_bool(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Script that evaluates to true */
	const uint8_t script_true[]  = "true";
	/* Script that evaluates to false */
	const uint8_t script_false[] = "false";

	bool r1 = js_exec(thread, script_true,  sizeof(script_true)  - 1, "true");
	bool r2 = js_exec(thread, script_false, sizeof(script_false) - 1, "false");

	CHECK(r1 == true,  "js_exec 'true' returns true");
	CHECK(r2 == false, "js_exec 'false' returns false");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_exec_syntax_error(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Deliberately broken JS */
	const uint8_t bad[] = "{{{{";
	bool ok = js_exec(thread, bad, sizeof(bad) - 1, "bad_script");
	CHECK(ok == false, "js_exec syntax error returns false");

	/* Thread must still be usable after an error */
	const uint8_t good[] = "2 + 2";
	bool ok2 = js_exec(thread, good, sizeof(good) - 1, "after_error");
	CHECK(ok2, "thread still usable after error");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_exec_null_safety(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	bool r1 = js_exec(thread, NULL, 10, "null_txt");
	bool r2 = js_exec(thread, (const uint8_t *)"x", 0, "zero_len");
	bool r3 = js_exec(NULL,   (const uint8_t *)"x", 1, "null_thread");

	CHECK(r1 == false, "js_exec NULL txt returns false");
	CHECK(r2 == false, "js_exec zero len returns false");
	CHECK(r3 == false, "js_exec NULL thread returns false");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_close_then_destroy(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	nserror err = js_closethread(thread);
	CHECK(err == NSERROR_OK, "js_closethread returns OK");

	js_destroythread(thread);
	js_destroyheap(heap);
	CHECK(1, "close+destroy sequence does not crash");
}

static void test_memory_limit_prevents_oom(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Allocate a huge array inside a function — should throw a JS exception
	 * (not crash) if the memory limit is set low enough (e.g. 8MB).
	 * Using a function scope so the array is GC-reclaimable after the exception. */
	const uint8_t script[] =
		"(function() { var a = []; for (var i = 0; i < 10000000; i++) a.push({x:i,y:i*2,z:'hello world ' + i}); })()";
	bool ok = js_exec(thread, script, sizeof(script) - 1, "oom_test");
	CHECK(ok == false, "huge alloc triggers JS exception, not crash");

	/* Thread must still be usable after OOM exception */
	const uint8_t good[] = "1 + 1";
	bool ok2 = js_exec(thread, good, sizeof(good) - 1, "after_oom");
	CHECK(ok2, "thread still usable after OOM exception");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_gc_threshold_set(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Allocate objects then check that GC can reclaim them.
	 * If GC threshold is set low, GC runs eagerly and keeps heap small. */
	const uint8_t script[] =
		"for (var i = 0; i < 1000; i++) { var o = {a:1,b:2}; }; true";
	bool ok = js_exec(thread, script, sizeof(script) - 1, "gc_test");
	CHECK(ok, "GC threshold allows normal alloc to succeed");

	/* Verify memory usage is reasonable (under 2MB after small allocs + GC) */
	JSMemoryUsage stats;
	JS_ComputeMemoryUsage(heap->rt, &stats);
	CHECK(stats.memory_used_size < 2 * 1024 * 1024,
	      "memory usage stays low with aggressive GC threshold");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_periodic_gc_in_exec(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Run many separate small scripts (simulates Acid3 pattern).
	 * With periodic GC in js_exec, memory should stay bounded. */
	for (int i = 0; i < 100; i++) {
		const uint8_t script[] =
			"var tmp = []; for (var j = 0; j < 100; j++) tmp.push({v:j}); true";
		js_exec(thread, script, sizeof(script) - 1, "burst");
	}

	JSMemoryUsage stats;
	JS_ComputeMemoryUsage(heap->rt, &stats);
	CHECK(stats.memory_used_size < 4 * 1024 * 1024,
	      "memory bounded after 100 burst script evals with periodic GC");

	js_destroythread(thread);
	js_destroyheap(heap);
}

static void test_memory_logging(void)
{
	jsheap *heap = NULL;
	jsthread *thread = NULL;

	js_newheap(10, &heap);
	js_newthread(heap, NULL, NULL, &thread);

	/* Run enough scripts to trigger a memory log (every 10th exec).
	 * We can't easily capture stderr in a unit test, so we just verify
	 * that the exec_count field is incremented. */
	for (int i = 0; i < 15; i++) {
		const uint8_t script[] = "1 + 1";
		js_exec(thread, script, sizeof(script) - 1, "count");
	}
	CHECK(heap->exec_count == 15,
	      "exec_count tracks number of js_exec calls");

	js_destroythread(thread);
	js_destroyheap(heap);
}

int main(void)
{
	fprintf(stderr, "=== qjscore Phase 2 smoke tests ===\n");

	test_heap_create_destroy();
	test_thread_create_destroy();
	test_exec_trivial();
	test_exec_returns_bool();
	test_exec_syntax_error();
	test_exec_null_safety();
	test_close_then_destroy();
	test_memory_limit_prevents_oom();
	test_gc_threshold_set();
	test_periodic_gc_in_exec();
	test_memory_logging();

	fprintf(stderr, "\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
	return tests_failed == 0 ? 0 : 1;
}
