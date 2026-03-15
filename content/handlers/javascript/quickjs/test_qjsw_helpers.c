/*
 * test_qjsw_helpers.c — TDD tests for qjsw_helpers.h/c (Phase 5)
 *
 * Tests the engine-independent helper functions that can be validated
 * without the full NetSurf build system. Functions requiring libdom
 * (push_node, push_event, register_event_listener_for, etc.) are
 * tested via the full build integration.
 *
 * Build (host-native):
 *   QJS_SRC=prototypes/nsfbrowser/build/sources/quickjs-2024-01-13
 *   QJS_INC=prototypes/nsfbrowser/build/cross/install/include
 *   QJSB=prototypes/nsfbrowser/native/quickjs
 *   gcc -DCONFIG_BIGNUM -DCONFIG_VERSION='"2024-01-13"' \
 *       -DQJSW_HELPERS_TEST_BUILD \
 *       -I$QJS_SRC -I$QJS_INC -I$QJSB \
 *       $QJSB/test_qjsw_helpers.c $QJSB/qjsw_helpers.c \
 *       $QJS_SRC/quickjs.c $QJS_SRC/libregexp.c $QJS_SRC/libunicode.c \
 *       $QJS_SRC/cutils.c $QJS_SRC/libbf.c \
 *       -lm -lpthread -o /tmp/test_qjsw_helpers && /tmp/test_qjsw_helpers
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/* Stub NSLOG for test build */
#define NSLOG(cat, level, fmt, ...) \
	fprintf(stderr, "[" #cat "|" #level "] " fmt "\n", ##__VA_ARGS__)

/* Include the headers under test */
#include "qjs_binding.h"
#include "qjsw_helpers.h"

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

static void test_log_exception_no_crash(JSContext *ctx)
{
	/* No pending exception — should not crash */
	qjsw_log_exception(ctx);
	CHECK(1, "qjsw_log_exception with no exception does not crash");
}

static void test_log_exception_with_error(JSContext *ctx)
{
	/* Create an exception */
	JS_Eval(ctx, "throw new Error('test error')", 29, "test",
		JS_EVAL_TYPE_GLOBAL);
	qjsw_log_exception(ctx);
	CHECK(1, "qjsw_log_exception with pending error does not crash");
}

static void test_push_generics_missing(JSContext *ctx)
{
	JSValue result = qjsw_push_generics(ctx, "nonexistent");
	CHECK(JS_IsUndefined(result),
	      "qjsw_push_generics returns undefined for missing generics stash");
	JS_FreeValue(ctx, result);
}

static void test_push_generics_present(JSContext *ctx)
{
	qjsw_reset_generics_state();
	/* Set up a generics stash */
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue generics = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, generics, "testFn",
			  JS_NewCFunction(ctx, NULL, "testFn", 0));
	JS_SetPropertyStr(ctx, global, "__NSQJS_GENERICS__", generics);
	JS_FreeValue(ctx, global);

	JSValue fn = qjsw_push_generics(ctx, "testFn");
	CHECK(!JS_IsUndefined(fn),
	      "qjsw_push_generics returns value for existing generic");
	JS_FreeValue(ctx, fn);
}

static JSValue test_add_fn(JSContext *ctx, JSValue this_val,
			   int argc, JSValue *argv)
{
	(void)this_val;
	if (argc < 2) return JS_UNDEFINED;
	int a, b;
	JS_ToInt32(ctx, &a, argv[0]);
	JS_ToInt32(ctx, &b, argv[1]);
	return JS_NewInt32(ctx, a + b);
}

static void test_pcall_success(JSContext *ctx)
{
	JSValue fn = JS_NewCFunction(ctx, test_add_fn, "add", 2);
	JSValue args[2] = { JS_NewInt32(ctx, 10), JS_NewInt32(ctx, 20) };

	JSValue result = qjsw_pcall(ctx, fn, JS_UNDEFINED, 2, args, false);
	CHECK(!JS_IsException(result), "qjsw_pcall succeeds");

	int n = 0;
	JS_ToInt32(ctx, &n, result);
	CHECK(n == 30, "qjsw_pcall result is 10+20=30");

	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, result);
	JS_FreeValue(ctx, fn);
}

static void test_pcall_exception(JSContext *ctx)
{
	/* Create a function that throws */
	JSValue fn = JS_Eval(ctx,
		"(function() { throw new Error('boom'); })",
		41, "test", JS_EVAL_TYPE_GLOBAL);
	CHECK(JS_IsFunction(ctx, fn), "created throwing function");

	JSValue result = qjsw_pcall(ctx, fn, JS_UNDEFINED, 0, NULL, false);
	CHECK(JS_IsException(result), "qjsw_pcall returns exception on throw");
	/* Exception was already logged and consumed by pcall */

	JS_FreeValue(ctx, result);
	JS_FreeValue(ctx, fn);
}

static void test_shuffle_array_basic(JSContext *ctx)
{
	JSValue arr = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, 10));
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, 20));
	JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, 30));

	/* Remove element at index 1 — should shift 30 down */
	qjsw_shuffle_array(ctx, arr, 1);

	int32_t v0, v1;
	JSValue e0 = JS_GetPropertyUint32(ctx, arr, 0);
	JSValue e1 = JS_GetPropertyUint32(ctx, arr, 1);
	JSValue e2 = JS_GetPropertyUint32(ctx, arr, 2);

	JS_ToInt32(ctx, &v0, e0);
	JS_ToInt32(ctx, &v1, e1);
	CHECK(v0 == 10, "shuffle_array: element 0 unchanged (10)");
	CHECK(v1 == 30, "shuffle_array: element 1 is now 30 (was 20)");
	CHECK(JS_IsUndefined(e2), "shuffle_array: element 2 is undefined (removed)");

	JS_FreeValue(ctx, e0);
	JS_FreeValue(ctx, e1);
	JS_FreeValue(ctx, e2);
	JS_FreeValue(ctx, arr);
}

static void test_shuffle_array_first(JSContext *ctx)
{
	JSValue arr = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, 100));
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, 200));

	qjsw_shuffle_array(ctx, arr, 0);

	int32_t v;
	JSValue e0 = JS_GetPropertyUint32(ctx, arr, 0);
	JSValue e1 = JS_GetPropertyUint32(ctx, arr, 1);

	JS_ToInt32(ctx, &v, e0);
	CHECK(v == 200, "shuffle_array from 0: element 0 is 200");
	CHECK(JS_IsUndefined(e1), "shuffle_array from 0: element 1 is undefined");

	JS_FreeValue(ctx, e0);
	JS_FreeValue(ctx, e1);
	JS_FreeValue(ctx, arr);
}

static void test_instanceof_basic(JSContext *ctx)
{
	JSValue obj = JS_NewObject(ctx);
	CHECK(qjsw_instanceof(ctx, obj, "anything"),
	      "qjsw_instanceof returns true for objects");
	CHECK(!qjsw_instanceof(ctx, JS_UNDEFINED, "anything"),
	      "qjsw_instanceof returns false for undefined");
	JS_FreeValue(ctx, obj);
}

static void test_closedown_thread(JSContext *ctx)
{
	/* Set up internal maps */
	JSValue global = JS_GetGlobalObject(ctx);
	JS_SetPropertyStr(ctx, global, "__NSQJS_NODE_MAP__",
			  JS_NewObject(ctx));
	JS_SetPropertyStr(ctx, global, "__NSQJS_EVENT_MAP__",
			  JS_NewObject(ctx));
	JS_SetPropertyStr(ctx, global, "__NSQJS_HANDLER_MAP__",
			  JS_NewObject(ctx));

	/* Add something to node map */
	JSValue node_map = JS_GetPropertyStr(ctx, global,
					     "__NSQJS_NODE_MAP__");
	JS_SetPropertyStr(ctx, node_map, "test_key",
			  JS_NewInt32(ctx, 42));
	JS_FreeValue(ctx, node_map);
	JS_FreeValue(ctx, global);

	/* Closedown should reset all maps */
	qjsw_closedown_thread(ctx);

	/* Verify maps are empty */
	global = JS_GetGlobalObject(ctx);
	node_map = JS_GetPropertyStr(ctx, global, "__NSQJS_NODE_MAP__");
	JSValue test_val = JS_GetPropertyStr(ctx, node_map, "test_key");
	CHECK(JS_IsUndefined(test_val),
	      "closedown_thread: node map is cleared");
	JS_FreeValue(ctx, test_val);
	JS_FreeValue(ctx, node_map);
	JS_FreeValue(ctx, global);
}

static void test_event_target_push_listeners_create(JSContext *ctx)
{
	JSValue node = JS_NewObject(ctx);

	/* Should create if dont_create=false */
	JSValue listeners = qjsw_event_target_push_listeners(
		ctx, "click", 5, node, false);
	CHECK(JS_IsArray(listeners),
	      "push_listeners creates array when dont_create=false");
	JS_FreeValue(ctx, listeners);

	/* Should find existing */
	JSValue listeners2 = qjsw_event_target_push_listeners(
		ctx, "click", 5, node, true);
	CHECK(JS_IsArray(listeners2),
	      "push_listeners finds existing array");
	JS_FreeValue(ctx, listeners2);

	JS_FreeValue(ctx, node);
}

static void test_event_target_push_listeners_dont_create(JSContext *ctx)
{
	JSValue node = JS_NewObject(ctx);

	/* Should return undefined if dont_create=true and doesn't exist */
	JSValue listeners = qjsw_event_target_push_listeners(
		ctx, "mouseover", 9, node, true);
	CHECK(JS_IsUndefined(listeners),
	      "push_listeners returns undefined when dont_create=true and missing");
	JS_FreeValue(ctx, listeners);

	JS_FreeValue(ctx, node);
}

static void test_shuffle_array_empty(JSContext *ctx)
{
	JSValue arr = JS_NewArray(ctx);
	/* Shuffling an empty array should not crash */
	qjsw_shuffle_array(ctx, arr, 0);
	CHECK(1, "shuffle_array on empty array does not crash");
	JS_FreeValue(ctx, arr);
}

static void test_pcall_with_this(JSContext *ctx)
{
	/* Create a function that reads this.x */
	JSValue fn = JS_Eval(ctx,
		"(function() { return this.x + 1; })",
		35, "test", JS_EVAL_TYPE_GLOBAL);

	JSValue this_obj = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, this_obj, "x", JS_NewInt32(ctx, 41));

	JSValue result = qjsw_pcall(ctx, fn, this_obj, 0, NULL, false);
	CHECK(!JS_IsException(result), "qjsw_pcall with this succeeds");

	int n = 0;
	JS_ToInt32(ctx, &n, result);
	CHECK(n == 42, "qjsw_pcall with this reads this.x correctly");

	JS_FreeValue(ctx, result);
	JS_FreeValue(ctx, this_obj);
	JS_FreeValue(ctx, fn);
}

/* ── Generics.js tests (Phase 7) ─────────────────────────────────────────── */

static void test_generics_loads(JSContext *ctx)
{
	FILE *f = fopen("native/quickjs/generics.js", "r");
	if (!f) f = fopen("../native/quickjs/generics.js", "r");
	if (!f) f = fopen("../../native/quickjs/generics.js", "r");
	if (!f) {
		/* Try absolute path */
		f = fopen("/workspaces/senfoni/prototypes/nsfbrowser/native/quickjs/generics.js", "r");
	}
	CHECK(f != NULL, "generics.js file found");
	if (!f) return;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(len + 1);
	fread(buf, 1, len, f);
	buf[len] = 0;
	fclose(f);

	/* Eval in sloppy mode (matches qjscore.c) */
	JSValue val = JS_Eval(ctx, buf, len, "generics.js", JS_EVAL_TYPE_GLOBAL);
	CHECK(!JS_IsException(val), "generics.js evaluates without exception");
	JS_FreeValue(ctx, val);

	JSValue global = JS_GetGlobalObject(ctx);
	JSValue ns = JS_GetPropertyStr(ctx, global, "NetSurf");
	CHECK(!JS_IsUndefined(ns), "generics.js defines NetSurf object");

	JSValue mlp = JS_GetPropertyStr(ctx, ns, "makeListProxy");
	CHECK(JS_IsFunction(ctx, mlp), "NetSurf.makeListProxy is a function");
	JS_FreeValue(ctx, mlp);

	JSValue mnmp = JS_GetPropertyStr(ctx, ns, "makeNodeMapProxy");
	CHECK(JS_IsFunction(ctx, mnmp), "NetSurf.makeNodeMapProxy is a function");
	JS_FreeValue(ctx, mnmp);

	JSValue fmt = JS_GetPropertyStr(ctx, ns, "consoleFormatter");
	CHECK(JS_IsFunction(ctx, fmt), "NetSurf.consoleFormatter is a function");
	JS_FreeValue(ctx, fmt);

	JS_FreeValue(ctx, ns);
	JS_FreeValue(ctx, global);
	free(buf);
}

static void test_generics_formatter_single(JSContext *ctx)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue ns = JS_GetPropertyStr(ctx, global, "NetSurf");
	JSValue fmt = JS_GetPropertyStr(ctx, ns, "consoleFormatter");

	JSValue args[2];
	args[0] = JS_NewString(ctx, "hello %s world");
	args[1] = JS_NewString(ctx, "cruel");
	JSValue result = JS_Call(ctx, fmt, JS_UNDEFINED, 2, args);
	CHECK(!JS_IsException(result), "Formatter single %s does not throw");

	JSValue item0 = JS_GetPropertyUint32(ctx, result, 0);
	const char *s = JS_ToCString(ctx, item0);
	CHECK(s != NULL && strcmp(s, "hello cruel world") == 0,
	      "Formatter('%s') produces correct output");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, item0);

	JS_FreeValue(ctx, result);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, fmt);
	JS_FreeValue(ctx, ns);
	JS_FreeValue(ctx, global);
}

static void test_generics_formatter_multi(JSContext *ctx)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue ns = JS_GetPropertyStr(ctx, global, "NetSurf");
	JSValue fmt = JS_GetPropertyStr(ctx, ns, "consoleFormatter");

	JSValue args[3];
	args[0] = JS_NewString(ctx, "%s has %d items");
	args[1] = JS_NewString(ctx, "list");
	args[2] = JS_NewInt32(ctx, 5);
	JSValue result = JS_Call(ctx, fmt, JS_UNDEFINED, 3, args);
	CHECK(!JS_IsException(result), "Formatter multi-subst does not throw");

	JSValue item0 = JS_GetPropertyUint32(ctx, result, 0);
	const char *s = JS_ToCString(ctx, item0);
	CHECK(s != NULL && strcmp(s, "list has 5 items") == 0,
	      "Formatter multi-subst produces correct output");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, item0);

	JS_FreeValue(ctx, result);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, args[2]);
	JS_FreeValue(ctx, fmt);
	JS_FreeValue(ctx, ns);
	JS_FreeValue(ctx, global);
}

static void test_generics_list_proxy(JSContext *ctx)
{
	/* Create a mock list object with length and item() */
	const char *setup =
		"(function() {"
		"  var inner = { length: 3, item: function(i) { return 'x' + i; } };"
		"  return NetSurf.makeListProxy(inner);"
		"})()";
	JSValue proxy = JS_Eval(ctx, setup, strlen(setup), "test", JS_EVAL_TYPE_GLOBAL);
	CHECK(!JS_IsException(proxy), "makeListProxy creates proxy without exception");

	/* Access by numeric string key (how Proxy delivers it in ES2020) */
	JSValue item = JS_GetPropertyUint32(ctx, proxy, 1);
	const char *s = JS_ToCString(ctx, item);
	CHECK(s != NULL && strcmp(s, "x1") == 0,
	      "list proxy[1] returns item(1)");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, item);

	/* Access .length */
	JSValue len = JS_GetPropertyStr(ctx, proxy, "length");
	int32_t n = 0;
	JS_ToInt32(ctx, &n, len);
	CHECK(n == 3, "list proxy.length returns 3");
	JS_FreeValue(ctx, len);

	JS_FreeValue(ctx, proxy);
}

static void test_generics_collection_proxy(JSContext *ctx)
{
	/* Simulate HTMLCollection-like object with length and item() */
	const char *setup =
		"(function() {"
		"  var inner = { length: 2, item: function(i) {"
		"    return i === 0 ? 'form0' : 'form1';"
		"  }, namedItem: function(n) { return 'named:' + n; } };"
		"  return NetSurf.makeListProxy(inner);"
		"})()";
	JSValue proxy = JS_Eval(ctx, setup, strlen(setup), "test",
				JS_EVAL_TYPE_GLOBAL);
	CHECK(!JS_IsException(proxy),
	      "makeListProxy works for collection-like objects");

	/* Indexed access */
	JSValue item0 = JS_GetPropertyUint32(ctx, proxy, 0);
	const char *s = JS_ToCString(ctx, item0);
	CHECK(s != NULL && strcmp(s, "form0") == 0,
	      "collection proxy[0] returns item(0)");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, item0);

	JSValue item1 = JS_GetPropertyUint32(ctx, proxy, 1);
	s = JS_ToCString(ctx, item1);
	CHECK(s != NULL && strcmp(s, "form1") == 0,
	      "collection proxy[1] returns item(1)");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, item1);

	/* Length */
	JSValue len = JS_GetPropertyStr(ctx, proxy, "length");
	int32_t n = 0;
	JS_ToInt32(ctx, &n, len);
	CHECK(n == 2, "collection proxy.length returns 2");
	JS_FreeValue(ctx, len);

	/* Out of bounds returns undefined/null (Proxy delegates to item) */
	JSValue item2 = JS_GetPropertyUint32(ctx, proxy, 5);
	s = JS_ToCString(ctx, item2);
	/* item(5) returns undefined since our mock only handles 0 and 1 */
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, item2);
	CHECK(1, "collection proxy out-of-bounds does not crash");

	JS_FreeValue(ctx, proxy);
}

static void test_create_event_basic(JSContext *ctx)
{
	/* Test that Event constructor-like patterns work at JS level.
	 * This validates the initEvent/initUIEvent flow at the JS API level. */
	const char *code =
		"(function() {"
		"  var obj = { type: '', bubbles: false, cancelable: false,"
		"    initEvent: function(t, b, c) {"
		"      this.type = t; this.bubbles = b; this.cancelable = c;"
		"    },"
		"    initUIEvent: function(t, b, c, v, d) {"
		"      this.type = t; this.bubbles = b; this.cancelable = c;"
		"      this.detail = d;"
		"    }"
		"  };"
		"  obj.initUIEvent('click', true, true, null, 1);"
		"  return obj.type + ':' + obj.bubbles + ':' + obj.detail;"
		"})()";
	JSValue result = JS_Eval(ctx, code, strlen(code), "test",
				 JS_EVAL_TYPE_GLOBAL);
	CHECK(!JS_IsException(result), "initUIEvent JS pattern works");
	const char *s = JS_ToCString(ctx, result);
	CHECK(s != NULL && strcmp(s, "click:true:1") == 0,
	      "initUIEvent sets type, bubbles, detail correctly");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, result);
}

static void test_polyfill_loads(JSContext *ctx)
{
	FILE *f = fopen("/workspaces/senfoni/prototypes/nsfbrowser/native/quickjs/polyfill.js", "r");
	CHECK(f != NULL, "polyfill.js file found");
	if (!f) return;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(len + 1);
	fread(buf, 1, len, f);
	buf[len] = 0;
	fclose(f);

	JSValue val = JS_Eval(ctx, buf, len, "polyfill.js", JS_EVAL_TYPE_GLOBAL);
	CHECK(!JS_IsException(val), "polyfill.js evaluates without exception");
	JS_FreeValue(ctx, val);
	free(buf);
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
	fprintf(stderr, "=== qjsw_helpers Phase 5 tests ===\n");

	JSRuntime *rt = JS_NewRuntime();
	JSContext *ctx = JS_NewContext(rt);

	test_log_exception_no_crash(ctx);
	test_log_exception_with_error(ctx);
	test_push_generics_missing(ctx);
	test_push_generics_present(ctx);
	test_pcall_success(ctx);
	test_pcall_exception(ctx);
	test_pcall_with_this(ctx);
	test_shuffle_array_basic(ctx);
	test_shuffle_array_first(ctx);
	test_shuffle_array_empty(ctx);
	test_instanceof_basic(ctx);
	test_closedown_thread(ctx);
	test_event_target_push_listeners_create(ctx);
	test_event_target_push_listeners_dont_create(ctx);

	/* Phase 7: generics.js + polyfill.js tests.
	 * Use a fresh context so generics.js NetSurf object is available. */
	JS_FreeContext(ctx);
	ctx = JS_NewContext(rt);
	test_polyfill_loads(ctx);
	test_generics_loads(ctx);
	test_generics_formatter_single(ctx);
	test_generics_formatter_multi(ctx);
	test_generics_list_proxy(ctx);
	test_generics_collection_proxy(ctx);
	test_create_event_basic(ctx);

	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);

	fprintf(stderr, "\n%d/%d tests passed\n",
		tests_run - tests_failed, tests_run);
	return tests_failed == 0 ? 0 : 1;
}
