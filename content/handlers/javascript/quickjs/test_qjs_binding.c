/*
 * test_qjs_binding.c — TDD tests for qjs_binding.h (Phase 3)
 *
 * Verifies every QJSB_* macro produces correct JSValues when compiled
 * against host-native QuickJS.  No NetSurf types needed — pure QuickJS.
 *
 * Build (host-native, run in repo root):
 *   QJS_SRC=prototypes/nsfbrowser/build/sources/quickjs-2024-01-13
 *   QJS_INC=prototypes/nsfbrowser/build/cross/install/include
 *   QJSB=prototypes/nsfbrowser/native/quickjs
 *   gcc -DCONFIG_BIGNUM -DCONFIG_VERSION='"2024-01-13"' \
 *       -I$QJS_SRC -I$QJS_INC -I$QJSB \
 *       $QJSB/test_qjs_binding.c \
 *       $QJS_SRC/quickjs.c $QJS_SRC/libregexp.c $QJS_SRC/libunicode.c \
 *       $QJS_SRC/cutils.c $QJS_SRC/libbf.c \
 *       -lm -o /tmp/test_qjs_binding && /tmp/test_qjs_binding
 *
 * (See scripts/build-nsfbrowser.sh for the automated invocation.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/* Include qjs_binding.h — which pulls in quickjs/quickjs.h */
#include "qjs_binding.h"

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

/* ── Shared class for private-data tests ─────────────────────────────────── */

JSClassID foo_class_id = 0;  /* QJSB_GET_PRIVATE(obj, foo) uses foo_class_id */

static void setup_foo_class(JSRuntime *rt, JSContext *ctx)
{
	static bool initialised = false;
	if (initialised) return;
	JS_NewClassID(rt, &foo_class_id);
	JSClassDef def = { "Foo", .finalizer = NULL };
	JS_NewClass(rt, foo_class_id, &def);
	JSValue proto = JS_NewObject(ctx);
	JS_SetClassProto(ctx, foo_class_id, proto);
	initialised = true;
}

/* ── Helper: C function for QJSB_CALL test ───────────────────────────────── */

static JSValue cfunc_add(JSContext *ctx, JSValue this_val,
			 int argc, JSValue *argv)
{
	(void)this_val;
	if (argc < 2) return JS_UNDEFINED;
	int a, b;
	JS_ToInt32(ctx, &a, argv[0]);
	JS_ToInt32(ctx, &b, argv[1]);
	return JS_NewInt32(ctx, a + b);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Tests
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Value creation ───────────────────────────────────────────────────────── */

static void test_push_string(JSContext *ctx)
{
	const char *s = "hello";
	JSValue v = QJSB_PUSH_STRING(ctx, s, 5);
	CHECK(JS_IsString(v), "QJSB_PUSH_STRING produces a string");

	size_t len = 0;
	const char *cs = JS_ToCStringLen(ctx, &len, v);
	CHECK(len == 5 && memcmp(cs, "hello", 5) == 0,
	      "QJSB_PUSH_STRING content is correct");
	JS_FreeCString(ctx, cs);
	QJSB_FREE(ctx, v);
}

static void test_push_int(JSContext *ctx)
{
	JSValue v = QJSB_PUSH_INT(ctx, 42);
	int n = 0;
	JS_ToInt32(ctx, &n, v);
	CHECK(n == 42, "QJSB_PUSH_INT produces correct int32");
	QJSB_FREE(ctx, v);
}

static void test_push_uint(JSContext *ctx)
{
	JSValue v = QJSB_PUSH_UINT(ctx, 0xDEADBEEFu);
	uint32_t n = 0;
	JS_ToIndex(ctx, (uint64_t *)&n, v);
	CHECK((uint32_t)n == 0xDEADBEEFu,
	      "QJSB_PUSH_UINT produces correct uint32");
	QJSB_FREE(ctx, v);
}

static void test_push_bool(JSContext *ctx)
{
	JSValue vt = QJSB_PUSH_BOOL(ctx, 1);
	JSValue vf = QJSB_PUSH_BOOL(ctx, 0);
	CHECK(JS_ToBool(ctx, vt) == 1, "QJSB_PUSH_BOOL(1) is truthy");
	CHECK(JS_ToBool(ctx, vf) == 0, "QJSB_PUSH_BOOL(0) is falsy");
	QJSB_FREE(ctx, vt);
	QJSB_FREE(ctx, vf);
}

static void test_push_null(JSContext *ctx)
{
	JSValue v = QJSB_PUSH_NULL(ctx);
	CHECK(JS_IsNull(v), "QJSB_PUSH_NULL is null");
	/* JS_NULL is a tag constant — no free needed */
}

static void test_push_undefined(JSContext *ctx)
{
	JSValue v = QJSB_PUSH_UNDEFINED(ctx);
	CHECK(QJSB_IS_UNDEFINED(v), "QJSB_PUSH_UNDEFINED is undefined");
	/* JS_UNDEFINED is a tag constant — no free needed */
}

/* ── Property access ──────────────────────────────────────────────────────── */

static void test_prop_str_roundtrip(JSContext *ctx)
{
	JSValue obj = QJSB_NEW_OBJECT(ctx);

	JSValue val = QJSB_PUSH_INT(ctx, 99);
	int rc = QJSB_SET_PROP_STR(ctx, obj, "answer", val);
	/* JS_SetPropertyStr takes ownership of val — no QJSB_FREE for val */
	CHECK(rc >= 0, "QJSB_SET_PROP_STR returns non-negative on success");

	JSValue got = QJSB_GET_PROP_STR(ctx, obj, "answer");
	int n = 0;
	JS_ToInt32(ctx, &n, got);
	CHECK(n == 99, "QJSB_GET_PROP_STR retrieves correct value");
	QJSB_FREE(ctx, got);
	QJSB_FREE(ctx, obj);
}

static void test_missing_prop_is_undefined(JSContext *ctx)
{
	JSValue obj = QJSB_NEW_OBJECT(ctx);
	JSValue got = QJSB_GET_PROP_STR(ctx, obj, "nonexistent");
	CHECK(QJSB_IS_UNDEFINED(got), "Missing prop is undefined");
	QJSB_FREE(ctx, got);
	QJSB_FREE(ctx, obj);
}

/* ── Index property ───────────────────────────────────────────────────────── */

static void test_prop_idx(JSContext *ctx)
{
	JSValue arr = QJSB_NEW_ARRAY(ctx);
	JSValue elem = QJSB_PUSH_INT(ctx, 7);
	/* QJSB_SET_PROP_IDX takes ownership of elem */
	QJSB_SET_PROP_IDX(ctx, arr, 0, elem);

	JSValue got = QJSB_GET_PROP_IDX(ctx, arr, 0);
	int n = 0;
	JS_ToInt32(ctx, &n, got);
	CHECK(n == 7, "QJSB_SET/GET_PROP_IDX round-trips array element");
	QJSB_FREE(ctx, got);
	QJSB_FREE(ctx, arr);
}

/* ── Private pointer ──────────────────────────────────────────────────────── */

static void test_private_ptr(JSRuntime *rt, JSContext *ctx)
{
	setup_foo_class(rt, ctx);

	JSValue obj = JS_NewObjectClass(ctx, foo_class_id);

	int sentinel = 0xCAFE;
	QJSB_SET_PRIVATE(obj, &sentinel);

	int *got = (int *)QJSB_GET_PRIVATE(obj, foo);
	CHECK(got == &sentinel, "QJSB_SET/GET_PRIVATE round-trips pointer");
	CHECK(*got == 0xCAFE,   "QJSB_GET_PRIVATE pointer is dereferenceable");

	QJSB_FREE(ctx, obj);
}

static void test_private_null_before_set(JSRuntime *rt, JSContext *ctx)
{
	setup_foo_class(rt, ctx);
	JSValue obj = JS_NewObjectClass(ctx, foo_class_id);
	void *p = QJSB_GET_PRIVATE(obj, foo);
	CHECK(p == NULL, "QJSB_GET_PRIVATE returns NULL before QJSB_SET_PRIVATE");
	QJSB_FREE(ctx, obj);
}

/* ── Function creation and call ──────────────────────────────────────────── */

static void test_new_cfunc_and_call(JSContext *ctx)
{
	JSValue fn = QJSB_NEW_CFUNC(ctx, cfunc_add, "add", 2);
	CHECK(JS_IsFunction(ctx, fn), "QJSB_NEW_CFUNC produces a function");

	JSValue args[2] = {
		QJSB_PUSH_INT(ctx, 3),
		QJSB_PUSH_INT(ctx, 4),
	};
	JSValue result = QJSB_CALL(ctx, fn, JS_UNDEFINED, 2, args);
	CHECK(!QJSB_IS_EXCEPTION(result), "QJSB_CALL did not throw");

	int n = 0;
	JS_ToInt32(ctx, &n, result);
	CHECK(n == 7, "QJSB_CALL result is 3+4=7");

	QJSB_FREE(ctx, args[0]);
	QJSB_FREE(ctx, args[1]);
	QJSB_FREE(ctx, result);
	QJSB_FREE(ctx, fn);
}

/* ── Global object ────────────────────────────────────────────────────────── */

static void test_global(JSContext *ctx)
{
	JSValue g = QJSB_GLOBAL(ctx);
	CHECK(JS_IsObject(g), "QJSB_GLOBAL returns an object");
	/* Global has standard properties like 'Math' */
	JSValue math = QJSB_GET_PROP_STR(ctx, g, "Math");
	CHECK(JS_IsObject(math) && !QJSB_IS_UNDEFINED(math),
	      "QJSB_GLOBAL().Math is accessible");
	QJSB_FREE(ctx, math);
	QJSB_FREE(ctx, g);
}

/* ── Value lifecycle ──────────────────────────────────────────────────────── */

static void test_dup_and_free(JSContext *ctx)
{
	JSValue orig = QJSB_PUSH_STRING(ctx, "test", 4);
	JSValue copy = QJSB_DUP(ctx, orig);

	/* Both should be valid strings */
	CHECK(JS_IsString(orig), "QJSB_DUP original still valid");
	CHECK(JS_IsString(copy), "QJSB_DUP copy is a string");

	QJSB_FREE(ctx, orig);
	/* copy is still valid after orig is freed */
	size_t len = 0;
	const char *cs = QJSB_TO_CSTRING(ctx, copy, &len);
	CHECK(len == 4 && memcmp(cs, "test", 4) == 0,
	      "QJSB_TO_CSTRING on dup'd value is correct");
	QJSB_FREE_CSTRING(ctx, cs);
	QJSB_FREE(ctx, copy);
}

/* ── Type predicates ──────────────────────────────────────────────────────── */

static void test_is_undefined(JSContext *ctx)
{
	JSValue undef = JS_UNDEFINED;
	JSValue obj   = QJSB_NEW_OBJECT(ctx);

	CHECK( QJSB_IS_UNDEFINED(undef), "QJSB_IS_UNDEFINED(JS_UNDEFINED) true");
	CHECK(!QJSB_IS_UNDEFINED(obj),   "QJSB_IS_UNDEFINED(object) false");
	QJSB_FREE(ctx, obj);
}

static void test_is_exception(JSContext *ctx)
{
	/* Force an exception via invalid property access on non-object */
	JSValue exc_val = JS_GetPropertyStr(ctx, JS_NULL, "x");
	if (QJSB_IS_EXCEPTION(exc_val)) {
		/* Clear the pending exception */
		JSValue exc = JS_GetException(ctx);
		JS_FreeValue(ctx, exc);
	}
	CHECK(QJSB_IS_EXCEPTION(exc_val),
	      "QJSB_IS_EXCEPTION detects exception from bad property get");
	/* exc_val itself needs no free — exception tags have no refcount */
}

static void test_is_null(JSContext *ctx)
{
	JSValue n = QJSB_PUSH_NULL(ctx);
	JSValue u = JS_UNDEFINED;
	CHECK( QJSB_IS_NULL(n), "QJSB_IS_NULL(JS_NULL) true");
	CHECK(!QJSB_IS_NULL(u), "QJSB_IS_NULL(JS_UNDEFINED) false");
}

/* ── cstring helpers ──────────────────────────────────────────────────────── */

static void test_to_cstring(JSContext *ctx)
{
	JSValue v = QJSB_PUSH_STRING(ctx, "world", 5);
	size_t len = 0;
	const char *cs = QJSB_TO_CSTRING(ctx, v, &len);
	CHECK(len == 5, "QJSB_TO_CSTRING returns correct length");
	CHECK(memcmp(cs, "world", 5) == 0, "QJSB_TO_CSTRING content matches");
	QJSB_FREE_CSTRING(ctx, cs);
	QJSB_FREE(ctx, v);
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
	fprintf(stderr, "=== qjs_binding Phase 3 macro tests ===\n");

	JSRuntime *rt  = JS_NewRuntime();
	JSContext *ctx = JS_NewContext(rt);

	/* Value creation */
	test_push_string(ctx);
	test_push_int(ctx);
	test_push_uint(ctx);
	test_push_bool(ctx);
	test_push_null(ctx);
	test_push_undefined(ctx);

	/* Property access */
	test_prop_str_roundtrip(ctx);
	test_missing_prop_is_undefined(ctx);

	/* Array/index */
	test_prop_idx(ctx);

	/* Private data */
	test_private_ptr(rt, ctx);
	test_private_null_before_set(rt, ctx);

	/* Function and call */
	test_new_cfunc_and_call(ctx);

	/* Global */
	test_global(ctx);

	/* Lifecycle */
	test_dup_and_free(ctx);

	/* Type predicates */
	test_is_undefined(ctx);
	test_is_exception(ctx);
	test_is_null(ctx);

	/* cstring helpers */
	test_to_cstring(ctx);

	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);

	fprintf(stderr, "\n%d/%d tests passed\n",
		tests_run - tests_failed, tests_run);
	return tests_failed == 0 ? 0 : 1;
}
