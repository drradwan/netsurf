/*
 * qjs_binding.h — thin macro bridge between nsgenbind-generated C and QuickJS
 *
 * Copyright 2026 Senfoni Authors
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 *
 * Phase 3 of the QuickJS migration (docs/quickjs.md).
 *
 * PURPOSE
 * -------
 * nsgenbind emits Duktape-specific C code inside %{ }% blocks in the 67 .bnd
 * files.  Phase 4 will teach nsgenbind to emit QJSB_* macros instead.  This
 * header maps those macros to the QuickJS API so the generated binding.c
 * compiles against QuickJS with zero engine-specific code.
 *
 * PATTERN COVERAGE (based on analysis of all 67 .bnd files)
 * ----------------------------------------------------------
 *  Pattern in .bnd                  QJSB_* macro
 *  ───────────────────────────────  ──────────────────────────────────────
 *  duk_push_lstring(ctx,s,n)        QJSB_PUSH_STRING(ctx,s,n)
 *  duk_push_int / duk_push_number   QJSB_PUSH_INT(ctx,n)
 *  duk_push_uint                    QJSB_PUSH_UINT(ctx,n)
 *  duk_push_boolean                 QJSB_PUSH_BOOL(ctx,b)
 *  duk_push_null                    QJSB_PUSH_NULL(ctx)
 *  (implicit undefined return)      QJSB_PUSH_UNDEFINED(ctx)
 *  duk_get_prop_string              QJSB_GET_PROP_STR(ctx,obj,key)
 *  duk_put_prop_string              QJSB_SET_PROP_STR(ctx,obj,k,v)
 *  duk_get_prop_index/put_prop_idx  QJSB_GET/SET_PROP_IDX(ctx,arr,idx,v)
 *  PRIVATE_MAGIC + duk_get_pointer  QJSB_GET_PRIVATE(obj,cls)
 *  duk_push_pointer + PRIVATE_MAGIC QJSB_SET_PRIVATE(obj,ptr)
 *  duk_push_c_function              QJSB_NEW_CFUNC(ctx,fn,name,nargs)
 *  pcall / pcall_method             QJSB_CALL(ctx,fn,this_,n,av)
 *  duk_push_object                  QJSB_NEW_OBJECT(ctx)
 *  duk_push_array                   QJSB_NEW_ARRAY(ctx)
 *  duk_push_global_object           QJSB_GLOBAL(ctx)
 *  JS_FreeValue (ubiquitous)        QJSB_FREE(ctx,v)
 *  duk_dup                          QJSB_DUP(ctx,v)
 *  duk_safe_to_lstring              QJSB_TO_CSTRING(ctx,v,lenptr)
 *  JS_FreeCString (paired above)    QJSB_FREE_CSTRING(ctx,s)
 *  duk_is_undefined                 QJSB_IS_UNDEFINED(v)
 *  JS_IsException (error paths)     QJSB_IS_EXCEPTION(v)
 *  JS_IsNull (null checks)          QJSB_IS_NULL(v)
 *
 * OWNERSHIP NOTES
 * ---------------
 * QuickJS is reference-counted: every JSValue returned by an API call is
 * owned by the caller and must be freed with JS_FreeValue() unless it is
 * a tag constant (JS_NULL, JS_UNDEFINED, JS_TRUE, JS_FALSE, JS_EXCEPTION).
 * Tag constants are safe to "free" (JS_FreeValue is a no-op for them) but
 * the convention here is to not free them.
 *
 * JS_SetPropertyStr() and JS_SetPropertyUint32() CONSUME (steal) the value
 * argument — the caller must NOT free it separately.  The macros below mirror
 * this ownership so callers must follow the same rule when using QJSB_*.
 *
 * PRIVATE DATA (QJSB_GET/SET_PRIVATE)
 * ------------------------------------
 * Each binding class must declare a global JSClassID variable named
 * <class>_class_id.  The macro QJSB_GET_PRIVATE(obj, cls) expands to
 * JS_GetOpaque(obj, cls##_class_id), so for class "Node" you need:
 *
 *   JSClassID node_class_id;
 *
 * This matches what nsgenbind's QuickJS backend will emit in Phase 4.
 */

#ifndef QJS_BINDING_H
#define QJS_BINDING_H

#include <stdlib.h>
#include <string.h>
#include "quickjs/quickjs.h"

/* ── Value creation ──────────────────────────────────────────────────────── */

/** Push a string of known byte length (maps duk_push_lstring). */
#define QJSB_PUSH_STRING(ctx, str, len) \
	JS_NewStringLen((ctx), (str), (len))

/** Push a signed 32-bit integer (maps duk_push_int). */
#define QJSB_PUSH_INT(ctx, n) \
	JS_NewInt32((ctx), (int32_t)(n))

/** Push an unsigned 32-bit integer (maps duk_push_uint). */
#define QJSB_PUSH_UINT(ctx, n) \
	JS_NewUint32((ctx), (uint32_t)(n))

/** Push a boolean (maps duk_push_boolean). */
#define QJSB_PUSH_BOOL(ctx, b) \
	JS_NewBool((ctx), (b))

/**
 * The null and undefined tag values.  ctx is accepted but unused so call
 * sites can keep the (ctx, ...) style that is consistent with the rest.
 * JS_NULL / JS_UNDEFINED are compile-time constants — no heap allocation.
 */
#define QJSB_PUSH_NULL(ctx)      JS_NULL
#define QJSB_PUSH_UNDEFINED(ctx) JS_UNDEFINED

/* ── Property access ─────────────────────────────────────────────────────── */

/**
 * Read a named property (maps duk_get_prop_string).
 * Returns a new JSValue owned by the caller — must be freed with QJSB_FREE.
 */
#define QJSB_GET_PROP_STR(ctx, obj, key) \
	JS_GetPropertyStr((ctx), (obj), (key))

/**
 * Write a named property (maps duk_put_prop_string).
 * CONSUMES val — do NOT free val separately after this call.
 * Returns TRUE on success.
 */
#define QJSB_SET_PROP_STR(ctx, obj, k, v) \
	JS_SetPropertyStr((ctx), (obj), (k), (v))

/**
 * Read/write an array slot by uint32 index (maps duk_get/put_prop_index).
 * QJSB_SET_PROP_IDX CONSUMES val.
 */
#define QJSB_GET_PROP_IDX(ctx, arr, idx) \
	JS_GetPropertyUint32((ctx), (arr), (uint32_t)(idx))

#define QJSB_SET_PROP_IDX(ctx, arr, idx, val) \
	JS_SetPropertyUint32((ctx), (arr), (uint32_t)(idx), (val))

/* ── Private pointer (DOM opaque data) ───────────────────────────────────── */

/**
 * Get the C private pointer for a DOM object (replaces PRIVATE_MAGIC trick).
 * cls must be a class prefix whose JSClassID is declared as <cls>_class_id.
 *
 * Example:  Node *np = QJSB_GET_PRIVATE(obj, node);
 *   requires: extern JSClassID node_class_id;
 */
#define QJSB_GET_PRIVATE(obj, cls) \
	JS_GetOpaque((obj), cls##_class_id)

/**
 * Set the C private pointer on a DOM object (maps duk_push_pointer + stash).
 * The pointer is not ref-counted; the binding's JSClassDef.finalizer is
 * responsible for freeing it.
 */
#define QJSB_SET_PRIVATE(obj, ptr) \
	JS_SetOpaque((obj), (ptr))

/* ── Function creation and call ──────────────────────────────────────────── */

/**
 * Create a new C function value (maps duk_push_c_function).
 * The returned JSValue is owned by the caller.
 * nargs = expected arg count; use JS_CFUNC_constructor_or_func for ctors.
 */
#define QJSB_NEW_CFUNC(ctx, fn, name, nargs) \
	JS_NewCFunction((ctx), (fn), (name), (nargs))

/**
 * Call a JS function with error handling.
 * Returns the result JSValue owned by the caller; check QJSB_IS_EXCEPTION.
 * this_ should be JS_UNDEFINED for plain calls.
 */
#define QJSB_CALL(ctx, fn, this_, n, av) \
	JS_Call((ctx), (fn), (this_), (int)(n), (av))

/* ── Object and array construction ───────────────────────────────────────── */

/** Create a plain object (maps duk_push_object). */
#define QJSB_NEW_OBJECT(ctx) \
	JS_NewObject((ctx))

/** Create a plain array (maps duk_push_array). */
#define QJSB_NEW_ARRAY(ctx) \
	JS_NewArray((ctx))

/** Get the global object (maps duk_push_global_object). Caller must free. */
#define QJSB_GLOBAL(ctx) \
	JS_GetGlobalObject((ctx))

/* ── Value lifecycle ─────────────────────────────────────────────────────── */

/** Release a JSValue (no-op for tag constants). */
#define QJSB_FREE(ctx, v) \
	JS_FreeValue((ctx), (v))

/** Increment the reference count and return a copy of the handle. */
#define QJSB_DUP(ctx, v) \
	JS_DupValue((ctx), (v))

/* ── C string extraction ─────────────────────────────────────────────────── */

/**
 * Convert a JSValue to a C string and its byte length (maps duk_safe_to_lstring).
 * Note argument order difference from Duktape: lenptr comes BEFORE the value.
 * The returned pointer must be released with QJSB_FREE_CSTRING.
 *
 * Example:
 *   size_t n;
 *   const char *s = QJSB_TO_CSTRING(ctx, val, &n);
 *   // use s[0..n-1]
 *   QJSB_FREE_CSTRING(ctx, s);
 */
#define QJSB_TO_CSTRING(ctx, v, lenptr) \
	JS_ToCStringLen((ctx), (lenptr), (v))

/** Release the C string returned by QJSB_TO_CSTRING. */
#define QJSB_FREE_CSTRING(ctx, s) \
	JS_FreeCString((ctx), (s))

/* ── Type predicates ─────────────────────────────────────────────────────── */

/** True if the value is undefined (maps duk_is_undefined). */
#define QJSB_IS_UNDEFINED(v) JS_IsUndefined(v)

/** True if the value is the JS exception sentinel (maps JS_IsException). */
#define QJSB_IS_EXCEPTION(v) JS_IsException(v)

/** True if the value is null (maps JS_IsNull). */
#define QJSB_IS_NULL(v) JS_IsNull(v)

/* ── quickjs-ng compatibility ────────────────────────────────────────────── */

/**
 * JS_GetOpaqueAny — retrieve opaque data without class_id check.
 * bellard's quickjs had this as a custom patch; quickjs-ng provides
 * JS_GetAnyOpaque(obj, &class_id) which requires a non-NULL pointer.
 */
static inline void *JS_GetOpaqueAny(JSValueConst obj)
{
	JSClassID dummy;
	return JS_GetAnyOpaque(obj, &dummy);
}

#endif /* QJS_BINDING_H */
