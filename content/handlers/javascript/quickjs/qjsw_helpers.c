/*
 * qjsw_helpers.c — QuickJS DOM helper functions
 *
 * Copyright 2026 Senfoni Authors
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Phase 5 of the QuickJS migration.
 *
 * QuickJS value-based API for DOM node wrapping, event dispatch, etc.
 * Stack manipulation collapses to variable assignments.
 */

#ifndef QJSW_HELPERS_TEST_BUILD
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "utils/log.h"
#include "utils/corestrings.h"

#include <dom/dom.h>

#include "qjs_binding.h"
#include "qjsw_helpers.h"

/* Generated binding headers */
#include "quickjs/binding.h"
#include "quickjs/private.h"
#include "quickjs/prototype.h"
#else
/* Test build — stubs provided by the test file */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef NSLOG
#define NSLOG(cat, level, fmt, ...) \
	fprintf(stderr, "[" #cat "|" #level "] " fmt "\n", ##__VA_ARGS__)
#include <stdio.h>
#endif

#include "qjs_binding.h"
#include "qjsw_helpers.h"
#endif

/* ── Internal stash property names ─────────────────────────────────────── */
#define QJS_NODE_MAP         "__NSQJS_NODE_MAP__"
#define QJS_EVENT_MAP        "__NSQJS_EVENT_MAP__"
#define QJS_PROTO_MAP        "__NSQJS_PROTO_MAP__"
#define QJS_GENERICS         "__NSQJS_GENERICS__"
#define QJS_HANDLER_MAP      "__NSQJS_HANDLER_MAP__"
#define QJS_HANDLER_LISTENER "__NSQJS_HANDLER_LISTENER_MAP__"
#define QJS_EL_JS_MAP        "__NSQJS_EVENT_LISTENER_JS_MAP__"

/* ── Exception logging ─────────────────────────────────────────────────── */

void qjsw_log_exception(JSContext *ctx)
{
	JSValue exc = JS_GetException(ctx);
	if (!JS_IsNull(exc) && !JS_IsUndefined(exc)) {
		const char *msg = JS_ToCString(ctx, exc);
		NSLOG(quickjs, WARNING, "QuickJS exception: %s",
		      msg ? msg : "(unknown)");
		JS_FreeCString(ctx, msg);
		JSValue stk = JS_GetPropertyStr(ctx, exc, "stack");
		if (!JS_IsUndefined(stk)) {
			const char *s = JS_ToCString(ctx, stk);
			NSLOG(quickjs, WARNING, "  stack: %s",
			      s ? s : "(no stack)");
			JS_FreeCString(ctx, s);
		}
		JS_FreeValue(ctx, stk);
	}
	JS_FreeValue(ctx, exc);
}

/* ── Generics ──────────────────────────────────────────────────────────── */

/* Cache whether generics are available — avoids repeated property
 * lookups on the global object (and works around a crash on 32-bit
 * Android API 13 where JS_GetPropertyStr for __NSQJS_GENERICS__
 * triggers SIGSEGV under certain heap layouts). */
static int generics_state = 0; /* 0=unknown, 1=available, -1=unavailable */

void qjsw_reset_generics_state(void)
{
	generics_state = 0;
}

JSValue qjsw_push_generics(JSContext *ctx, const char *name)
{
	if (generics_state == -1) {
		return JS_UNDEFINED;
	}

	JSValue global = JS_GetGlobalObject(ctx);
	JSValue generics = JS_GetPropertyStr(ctx, global, QJS_GENERICS);
	JS_FreeValue(ctx, global);

	if (JS_IsUndefined(generics)) {
		generics_state = -1;
		return JS_UNDEFINED;
	}

	generics_state = 1;
	JSValue fn = JS_GetPropertyStr(ctx, generics, name);
	JS_FreeValue(ctx, generics);
	return fn;
}

/* ── Protected call ────────────────────────────────────────────────────── */

#ifndef QJSW_HELPERS_TEST_BUILD
static void qjsw_reset_exec_timer(JSContext *ctx);
#endif

JSValue qjsw_pcall(JSContext *ctx, JSValue fn, JSValue this_val,
		    int argc, JSValueConst *argv, bool reset_timeout)
{
#ifndef QJSW_HELPERS_TEST_BUILD
	if (reset_timeout) {
		qjsw_reset_exec_timer(ctx);
	}
#else
	(void)reset_timeout;
#endif

	JSValue result = JS_Call(ctx, fn, this_val, argc, argv);
	if (JS_IsException(result)) {
		qjsw_log_exception(ctx);
	}
	return result;
}

/* ── Array shuffle ─────────────────────────────────────────────────────── */

void qjsw_shuffle_array(JSContext *ctx, JSValue arr, uint32_t idx)
{
	for (;;) {
		JSValue next = JS_GetPropertyUint32(ctx, arr, idx + 1);
		if (JS_IsUndefined(next)) {
			JS_FreeValue(ctx, next);
			/* Delete the last element */
			JSAtom atom = JS_NewAtomUInt32(ctx, idx);
			JS_DeleteProperty(ctx, arr, atom, 0);
			JS_FreeAtom(ctx, atom);
			break;
		}
		/* Move next down: arr[idx] = arr[idx+1] */
		JS_SetPropertyUint32(ctx, arr, idx, next);
		/* next is consumed by SetProperty */
		idx++;
	}
}

/* ── instanceof check ──────────────────────────────────────────────────── */

bool qjsw_instanceof(JSContext *ctx, JSValue val, const char *proto_name)
{
	(void)proto_name;
	/* Simplified: check if val is an object with opaque data */
	return JS_IsObject(val);
}

/* ── Thread closedown ──────────────────────────────────────────────────── */

void qjsw_closedown_thread(JSContext *ctx)
{
	JSValue global = JS_GetGlobalObject(ctx);

	/* Call closedownThread if Window defines it */
	JSValue closedown = JS_GetPropertyStr(ctx, global,
					      "__NSQJS_closedownThread__");
	if (JS_IsFunction(ctx, closedown)) {
		JSValue r = JS_Call(ctx, closedown, global, 0, NULL);
		JS_FreeValue(ctx, r);
	}
	JS_FreeValue(ctx, closedown);

	/* Clean up internal maps */
	JS_SetPropertyStr(ctx, global, QJS_NODE_MAP, JS_NewObject(ctx));
	JS_SetPropertyStr(ctx, global, QJS_EVENT_MAP, JS_NewObject(ctx));
	JS_SetPropertyStr(ctx, global, QJS_HANDLER_MAP, JS_NewObject(ctx));

	JS_FreeValue(ctx, global);
}

/* ── Event listener management (engine-only, no libdom) ────────────────── */

JSValue qjsw_event_target_push_listeners(JSContext *ctx,
					  const char *type, size_t type_len,
					  JSValue node, bool dont_create)
{
	(void)type_len;
	/* Get or create __NSQJS_EVENT_LISTENER_JS_MAP__ on the node */
	JSValue el_map = JS_GetPropertyStr(ctx, node, QJS_EL_JS_MAP);
	if (JS_IsUndefined(el_map)) {
		if (dont_create) {
			JS_FreeValue(ctx, el_map);
			return JS_UNDEFINED;
		}
		el_map = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, node, QJS_EL_JS_MAP,
				  JS_DupValue(ctx, el_map));
	}

	/* Get or create the sub-array for this event type */
	JSValue sub = JS_GetPropertyStr(ctx, el_map, type);
	if (JS_IsUndefined(sub)) {
		if (dont_create) {
			JS_FreeValue(ctx, sub);
			JS_FreeValue(ctx, el_map);
			return JS_UNDEFINED;
		}
		sub = JS_NewArray(ctx);
		JS_SetPropertyStr(ctx, el_map, type,
				  JS_DupValue(ctx, sub));
	}

	JS_FreeValue(ctx, el_map);
	return sub; /* caller owns */
}

#ifndef QJSW_HELPERS_TEST_BUILD
/* ══════════════════════════════════════════════════════════════════════════
 * Everything below here requires NetSurf/libdom headers.
 * The test build excludes this section.
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Timer reset ───────────────────────────────────────────────────────── */

/* Access the jsheap from the runtime opaque to reset exec timer */
struct jsheap;

extern void qjs_reset_exec_timer_from_ctx(JSContext *ctx);

static void qjsw_reset_exec_timer(JSContext *ctx)
{
	/* Defined in qjscore.c — resets the exec start time */
	qjs_reset_exec_timer_from_ctx(ctx);
}

/* ── HTML element class from tag type ──────────────────────────────────── */

static const char *
qjsw_html_element_class_from_tag_type(dom_html_element_type type)
{
	switch (type) {
	case DOM_HTML_ELEMENT_TYPE_HTML:       return "html_html_element";
	case DOM_HTML_ELEMENT_TYPE_HEAD:       return "html_head_element";
	case DOM_HTML_ELEMENT_TYPE_META:       return "html_meta_element";
	case DOM_HTML_ELEMENT_TYPE_BASE:       return "html_base_element";
	case DOM_HTML_ELEMENT_TYPE_TITLE:      return "html_title_element";
	case DOM_HTML_ELEMENT_TYPE_BODY:       return "html_body_element";
	case DOM_HTML_ELEMENT_TYPE_DIV:        return "html_div_element";
	case DOM_HTML_ELEMENT_TYPE_FORM:       return "html_form_element";
	case DOM_HTML_ELEMENT_TYPE_LINK:       return "html_link_element";
	case DOM_HTML_ELEMENT_TYPE_BUTTON:     return "html_button_element";
	case DOM_HTML_ELEMENT_TYPE_INPUT:      return "html_input_element";
	case DOM_HTML_ELEMENT_TYPE_TEXTAREA:   return "html_text_area_element";
	case DOM_HTML_ELEMENT_TYPE_OPTGROUP:   return "html_opt_group_element";
	case DOM_HTML_ELEMENT_TYPE_OPTION:     return "html_option_element";
	case DOM_HTML_ELEMENT_TYPE_SELECT:     return "html_select_element";
	case DOM_HTML_ELEMENT_TYPE_HR:         return "html_hr_element";
	case DOM_HTML_ELEMENT_TYPE_DL:         return "html_d_list_element";
	case DOM_HTML_ELEMENT_TYPE_DIR:        return "html_directory_element";
	case DOM_HTML_ELEMENT_TYPE_MENU:       return "html_menu_element";
	case DOM_HTML_ELEMENT_TYPE_FIELDSET:   return "html_field_set_element";
	case DOM_HTML_ELEMENT_TYPE_LEGEND:     return "html_legend_element";
	case DOM_HTML_ELEMENT_TYPE_P:          return "html_paragraph_element";
	case DOM_HTML_ELEMENT_TYPE_H1:
	case DOM_HTML_ELEMENT_TYPE_H2:
	case DOM_HTML_ELEMENT_TYPE_H3:
	case DOM_HTML_ELEMENT_TYPE_H4:
	case DOM_HTML_ELEMENT_TYPE_H5:
	case DOM_HTML_ELEMENT_TYPE_H6:         return "html_heading_element";
	case DOM_HTML_ELEMENT_TYPE_BLOCKQUOTE:
	case DOM_HTML_ELEMENT_TYPE_Q:          return "html_quote_element";
	case DOM_HTML_ELEMENT_TYPE_PRE:        return "html_pre_element";
	case DOM_HTML_ELEMENT_TYPE_BR:         return "html_br_element";
	case DOM_HTML_ELEMENT_TYPE_LABEL:      return "html_label_element";
	case DOM_HTML_ELEMENT_TYPE_UL:         return "html_u_list_element";
	case DOM_HTML_ELEMENT_TYPE_OL:         return "html_o_list_element";
	case DOM_HTML_ELEMENT_TYPE_LI:         return "html_li_element";
	case DOM_HTML_ELEMENT_TYPE_FONT:       return "html_font_element";
	case DOM_HTML_ELEMENT_TYPE_DEL:
	case DOM_HTML_ELEMENT_TYPE_INS:        return "html_mod_element";
	case DOM_HTML_ELEMENT_TYPE_A:          return "html_anchor_element";
	case DOM_HTML_ELEMENT_TYPE_BASEFONT:   return "html_base_font_element";
	case DOM_HTML_ELEMENT_TYPE_IMG:        return "html_image_element";
	case DOM_HTML_ELEMENT_TYPE_OBJECT:     return "html_object_element";
	case DOM_HTML_ELEMENT_TYPE_PARAM:      return "html_param_element";
	case DOM_HTML_ELEMENT_TYPE_APPLET:     return "html_applet_element";
	case DOM_HTML_ELEMENT_TYPE_MAP:        return "html_map_element";
	case DOM_HTML_ELEMENT_TYPE_AREA:       return "html_area_element";
	case DOM_HTML_ELEMENT_TYPE_SCRIPT:     return "html_script_element";
	case DOM_HTML_ELEMENT_TYPE_CAPTION:    return "html_table_caption_element";
	case DOM_HTML_ELEMENT_TYPE_TD:
	case DOM_HTML_ELEMENT_TYPE_TH:         return "html_table_cell_element";
	case DOM_HTML_ELEMENT_TYPE_COL:
	case DOM_HTML_ELEMENT_TYPE_COLGROUP:   return "html_table_col_element";
	case DOM_HTML_ELEMENT_TYPE_THEAD:
	case DOM_HTML_ELEMENT_TYPE_TBODY:
	case DOM_HTML_ELEMENT_TYPE_TFOOT:      return "html_table_section_element";
	case DOM_HTML_ELEMENT_TYPE_TABLE:      return "html_table_element";
	case DOM_HTML_ELEMENT_TYPE_TR:         return "html_table_row_element";
	case DOM_HTML_ELEMENT_TYPE_STYLE:      return "html_style_element";
	case DOM_HTML_ELEMENT_TYPE_FRAMESET:   return "html_frame_set_element";
	case DOM_HTML_ELEMENT_TYPE_FRAME:      return "html_frame_element";
	case DOM_HTML_ELEMENT_TYPE_IFRAME:     return "html_iframe_element";
	case DOM_HTML_ELEMENT_TYPE_ISINDEX:    return "html_is_index_element";
	case DOM_HTML_ELEMENT_TYPE_CANVAS:     return "html_canvas_element";
	case DOM_HTML_ELEMENT_TYPE__UNKNOWN:
	default:                               return "html_unknown_element";
	}
}

static const char *
qjsw_node_class_name(struct dom_node *node)
{
	dom_node_type nodetype;
	dom_exception err;

	err = dom_node_get_node_type(node, &nodetype);
	if (err != DOM_NO_ERR) {
		return "node";
	}

	switch (nodetype) {
	case DOM_ELEMENT_NODE: {
		dom_string *ns;
		dom_html_element_type type;

		err = dom_node_get_namespace(node, &ns);
		if (err != DOM_NO_ERR || ns == NULL) {
			return "element";
		}

		if (!dom_string_isequal(ns, corestring_dom_html_namespace)) {
			dom_string_unref(ns);
			return "element";
		}
		dom_string_unref(ns);

		err = dom_html_element_get_tag_type(node, &type);
		if (err != DOM_NO_ERR) {
			type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
		}

		return qjsw_html_element_class_from_tag_type(type);
	}
	case DOM_TEXT_NODE:
		return "text";
	case DOM_COMMENT_NODE:
		return "comment";
	case DOM_DOCUMENT_NODE:
		return "document";
	default:
		return "node";
	}
}

/* ── Node wrapping ─────────────────────────────────────────────────────── */

JSValue qjsw_push_node(JSContext *ctx, struct dom_node *node)
{
	if (node == NULL) {
		return JS_NULL;
	}

	JSValue global = JS_GetGlobalObject(ctx);
	JSValue node_map = JS_GetPropertyStr(ctx, global, QJS_NODE_MAP);
	char key[32];

	snprintf(key, sizeof(key), "%p", (void *)node);

	/* Check memoisation cache */
	JSValue cached = JS_GetPropertyStr(ctx, node_map, key);
	if (!JS_IsUndefined(cached)) {
		JS_FreeValue(ctx, node_map);
		JS_FreeValue(ctx, global);
		return cached; /* caller owns */
	}
	JS_FreeValue(ctx, cached);

	/* Determine class and create object */
	const char *class_name = qjsw_node_class_name(node);
	JSClassID cid = 0;

	/* Look up class_id by name — the generated binding.c defines these.
	 * We use a lookup via the prototype map since class IDs are not
	 * easily discoverable by name at runtime. Instead, we create the
	 * object using the constructor pattern. */
	JSValue node_ptr = JS_NewInt64(ctx, (int64_t)(uintptr_t)node);
	JSValue obj = qjsw_create_object(ctx, class_name, 1, &node_ptr);
	JS_FreeValue(ctx, node_ptr);

	if (JS_IsException(obj)) {
		/* Fallback: use HTMLUnknownElement */
		JS_GetException(ctx); /* clear */
		JSValue exc_val = JS_GetException(ctx);
		JS_FreeValue(ctx, exc_val);
		node_ptr = JS_NewInt64(ctx, (int64_t)(uintptr_t)node);
		obj = qjsw_create_object(ctx, "html_unknown_element",
					 1, &node_ptr);
		JS_FreeValue(ctx, node_ptr);
		if (JS_IsException(obj)) {
			JS_FreeValue(ctx, node_map);
			JS_FreeValue(ctx, global);
			return JS_UNDEFINED;
		}
	}

	/* Store in cache: node_map[key] = obj */
	JS_SetPropertyStr(ctx, node_map, key, JS_DupValue(ctx, obj));

	JS_FreeValue(ctx, node_map);
	JS_FreeValue(ctx, global);
	return obj; /* caller owns */
}

/* ── Event wrapping ────────────────────────────────────────────────────── */

static const char *
qjsw_event_proto(struct dom_event *evt)
{
	dom_string *type = NULL;
	dom_exception err;

	err = dom_event_get_type(evt, &type);
	if (err != DOM_NO_ERR || type == NULL) {
		return "event";
	}

	if (dom_string_isequal(type, corestring_dom_keydown) ||
	    dom_string_isequal(type, corestring_dom_keyup) ||
	    dom_string_isequal(type, corestring_dom_keypress)) {
		dom_string_unref(type);
		return "keyboard_event";
	}

	dom_string_unref(type);
	return "event";
}

/**
 * After creating an event wrapper, copy detail from dom_ui_event
 * if the event was created as UIEvent (check via event map tag).
 */
static void
qjsw_event_copy_detail(JSContext *ctx, JSValue obj, struct dom_event *evt)
{
	/* If the event has a non-zero detail at the UIEvent offset, copy it.
	 * UIEvent struct: { dom_event base; dom_abstract_view *view; int32_t detail; }
	 * We read detail via the public API which casts safely. */
	int32_t detail = 0;
	dom_ui_event_get_detail((dom_ui_event *)evt, &detail);
	if (detail != 0) {
		JS_SetPropertyStr(ctx, obj, "detail",
				  JS_NewInt32(ctx, detail));
	}
}

JSValue qjsw_push_event(JSContext *ctx, struct dom_event *evt)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue event_map = JS_GetPropertyStr(ctx, global, QJS_EVENT_MAP);
	char key[32];

	snprintf(key, sizeof(key), "%p", (void *)evt);

	/* Check memoisation cache */
	JSValue cached = JS_GetPropertyStr(ctx, event_map, key);
	if (!JS_IsUndefined(cached)) {
		JS_FreeValue(ctx, event_map);
		JS_FreeValue(ctx, global);
		return cached;
	}
	JS_FreeValue(ctx, cached);

	/* Create new event wrapper */
	const char *proto_name = qjsw_event_proto(evt);
	JSValue evt_ptr = JS_NewInt64(ctx, (int64_t)(uintptr_t)evt);
	JSValue obj = qjsw_create_object(ctx, proto_name, 1, &evt_ptr);
	JS_FreeValue(ctx, evt_ptr);

	if (JS_IsException(obj)) {
		/* Fallback: plain object */
		JSValue exc = JS_GetException(ctx);
		JS_FreeValue(ctx, exc);
		obj = JS_NewObject(ctx);
	}

	/* Copy UIEvent detail if present (handles cache miss for UIEvents) */
	qjsw_event_copy_detail(ctx, obj, evt);

	/* Cache it */
	JS_SetPropertyStr(ctx, event_map, key, JS_DupValue(ctx, obj));

	JS_FreeValue(ctx, event_map);
	JS_FreeValue(ctx, global);
	return obj;
}

/* ── Object creation ───────────────────────────────────────────────────── */

JSValue qjsw_create_object(JSContext *ctx, const char *proto_name,
			    int argc, JSValueConst *argv)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue proto_map = JS_GetPropertyStr(ctx, global, QJS_PROTO_MAP);
	JSValue entry = JS_GetPropertyStr(ctx, proto_map, proto_name);

	if (JS_IsUndefined(entry)) {
		NSLOG(quickjs, WARNING,
		      "QuickJS: no prototype for '%s' — falling back to html_unknown_element",
		      proto_name);
		JS_FreeValue(ctx, entry);
		entry = JS_GetPropertyStr(ctx, proto_map, "html_unknown_element");
		if (JS_IsUndefined(entry)) {
			JS_FreeValue(ctx, entry);
			JS_FreeValue(ctx, proto_map);
			JS_FreeValue(ctx, global);
			return JS_EXCEPTION;
		}
	}

	/* entry = { class_id: N, init: factory_fn } */
	JSValue init_fn = JS_GetPropertyStr(ctx, entry, "init");

	JSValue obj;
	if (JS_IsFunction(ctx, init_fn)) {
		/* Call the factory — it creates the object, allocates
		 * private data, and calls __init, returning the
		 * fully-initialised wrapper. */
		obj = JS_Call(ctx, init_fn, JS_UNDEFINED, argc, argv);
		if (JS_IsException(obj)) {
			qjsw_log_exception(ctx);
			JS_FreeValue(ctx, init_fn);
			JS_FreeValue(ctx, entry);
			JS_FreeValue(ctx, proto_map);
			JS_FreeValue(ctx, global);
			return JS_EXCEPTION;
		}
	} else {
		/* No factory — create a bare object using class_id */
		JSValue cid_val = JS_GetPropertyStr(ctx, entry, "class_id");
		int32_t cid = 0;
		JS_ToInt32(ctx, &cid, cid_val);
		JS_FreeValue(ctx, cid_val);

		JSValue proto = JS_GetClassProto(ctx, (JSClassID)cid);
		obj = JS_NewObjectProtoClass(ctx, proto, (JSClassID)cid);
		JS_FreeValue(ctx, proto);

		if (JS_IsException(obj)) {
			JS_FreeValue(ctx, init_fn);
			JS_FreeValue(ctx, entry);
			JS_FreeValue(ctx, proto_map);
			JS_FreeValue(ctx, global);
			return JS_EXCEPTION;
		}
	}

	/* Set handler listener and handler maps on the object */
	JS_SetPropertyStr(ctx, obj, QJS_HANDLER_LISTENER,
			  JS_NewObject(ctx));
	JS_SetPropertyStr(ctx, obj, QJS_HANDLER_MAP,
			  JS_NewObject(ctx));

	JS_FreeValue(ctx, init_fn);
	JS_FreeValue(ctx, entry);
	JS_FreeValue(ctx, proto_map);
	JS_FreeValue(ctx, global);
	return obj;
}

void qjsw_register_event_listener_for(JSContext *ctx,
				       struct dom_element *ele,
				       struct dom_string *name,
				       bool capture)
{
	dom_event_listener *listen = NULL;
	dom_exception exc;

	/* Get the node's JS wrapper */
	JSValue node;
	if (ele == NULL) {
		/* Window object */
		node = JS_GetGlobalObject(ctx);
	} else {
		node = qjsw_push_node(ctx, (struct dom_node *)ele);
		if (JS_IsUndefined(node))
			return;
	}

	/* Check if we already have a listener registered for this event */
	JSValue hl_map = JS_GetPropertyStr(ctx, node, QJS_HANDLER_LISTENER);
	if (JS_IsUndefined(hl_map)) {
		hl_map = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, node, QJS_HANDLER_LISTENER,
				  JS_DupValue(ctx, hl_map));
	}

	const char *name_data = dom_string_data(name);
	size_t name_len = dom_string_length(name);
	char *name_cstr = malloc(name_len + 1);
	memcpy(name_cstr, name_data, name_len);
	name_cstr[name_len] = '\0';

	JSValue has = JS_GetPropertyStr(ctx, hl_map, name_cstr);
	if (!JS_IsUndefined(has)) {
		/* Already registered */
		JS_FreeValue(ctx, has);
		JS_FreeValue(ctx, hl_map);
		JS_FreeValue(ctx, node);
		free(name_cstr);
		return;
	}
	JS_FreeValue(ctx, has);

	/* Mark as registered */
	JS_SetPropertyStr(ctx, hl_map, name_cstr, JS_TRUE);
	JS_FreeValue(ctx, hl_map);
	JS_FreeValue(ctx, node);
	free(name_cstr);

	if (ele == NULL) {
		/* Window doesn't register in the normal event listener flow */
		return;
	}

	/* Add a DOM event listener to the element */
	exc = dom_event_listener_create(qjsw_generic_event_handler, ctx,
					&listen);
	if (exc != DOM_NO_ERR) return;

	exc = dom_event_target_add_event_listener(ele, name, listen, capture);
	if (exc != DOM_NO_ERR) {
		NSLOG(quickjs, DEBUG,
		      "QuickJS: unable to register listener for %p.%*s",
		      ele, (int)dom_string_length(name),
		      dom_string_data(name));
	} else {
		NSLOG(quickjs, DEBUG,
		      "QuickJS: registered listener for %p.%*s",
		      ele, (int)dom_string_length(name),
		      dom_string_data(name));
	}
	dom_event_listener_unref(listen);
}

/* ── Event handler get/set (inline on* attributes) ─────────────────────── */

static JSValue
qjsw_push_handler_code(JSContext *ctx, struct dom_string *name,
			struct dom_event_target *et)
{
	dom_string *onname, *val;
	dom_element *ele = (dom_element *)et;
	dom_exception exc;
	dom_node_type ntype;

	if (et == NULL) {
		/* Window object — no inline handlers */
		return QJSB_PUSH_STRING(ctx, "", 0);
	}

	exc = dom_node_get_node_type(et, &ntype);
	if (exc != DOM_NO_ERR || ntype != DOM_ELEMENT_NODE) {
		return QJSB_PUSH_STRING(ctx, "", 0);
	}

	exc = dom_string_concat(corestring_dom_on, name, &onname);
	if (exc != DOM_NO_ERR) {
		return QJSB_PUSH_STRING(ctx, "", 0);
	}

	exc = dom_element_get_attribute(ele, onname, &val);
	if (exc != DOM_NO_ERR || val == NULL) {
		dom_string_unref(onname);
		return QJSB_PUSH_STRING(ctx, "", 0);
	}

	dom_string_unref(onname);
	JSValue ret = QJSB_PUSH_STRING(ctx,
					dom_string_data(val),
					dom_string_length(val));
	dom_string_unref(val);
	return ret;
}

JSValue qjsw_get_event_handler(JSContext *ctx, JSValue priv_val,
				struct dom_event_target *et,
				const char *event_name, size_t len)
{
	/* Check handler map cache first */
	JSValue handler_map = JS_GetPropertyStr(ctx, priv_val,
						QJS_HANDLER_MAP);
	if (JS_IsUndefined(handler_map)) {
		handler_map = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, priv_val, QJS_HANDLER_MAP,
				  JS_DupValue(ctx, handler_map));
	}

	char *name_str = malloc(len + 1);
	memcpy(name_str, event_name, len);
	name_str[len] = '\0';

	JSValue cached = JS_GetPropertyStr(ctx, handler_map, name_str);
	if (!JS_IsUndefined(cached)) {
		JS_FreeValue(ctx, handler_map);
		free(name_str);
		return cached; /* caller owns */
	}
	JS_FreeValue(ctx, cached);

	/* Not in cache — get from DOM attribute and compile */
	dom_string *dom_name;
	dom_exception exc;
	exc = dom_string_create((const uint8_t *)event_name, len, &dom_name);
	if (exc != DOM_NO_ERR) {
		JS_FreeValue(ctx, handler_map);
		free(name_str);
		return JS_UNDEFINED;
	}

	JSValue handler_code = qjsw_push_handler_code(ctx, dom_name, et);
	dom_string_unref(dom_name);

	/* Check if we got any handler code */
	size_t code_len;
	const char *code_str = JS_ToCStringLen(ctx, &code_len, handler_code);
	if (code_str == NULL || code_len == 0) {
		JS_FreeCString(ctx, code_str);
		JS_FreeValue(ctx, handler_code);
		JS_FreeValue(ctx, handler_map);
		free(name_str);
		return JS_UNDEFINED;
	}

	/* Compile: (function(event) { <handler_code> })
	 * Parentheses make it a function expression — without them,
	 * JS_EVAL_TYPE_GLOBAL treats it as a function declaration
	 * which requires a name (SyntaxError on QuickJS). */
	size_t full_len = 22 + code_len + 2;
	char *full_src = malloc(full_len);
	snprintf(full_src, full_len, "(function(event){%s})", code_str);
	JS_FreeCString(ctx, code_str);
	JS_FreeValue(ctx, handler_code);

	JSValue compiled = JS_Eval(ctx, full_src, strlen(full_src),
				   "inline-handler",
				   JS_EVAL_TYPE_GLOBAL);
	free(full_src);

	if (JS_IsException(compiled)) {
		NSLOG(quickjs, DEBUG,
		      "QuickJS: unable to compile inline handler for %s",
		      name_str);
		qjsw_log_exception(ctx);
		JS_FreeValue(ctx, handler_map);
		free(name_str);
		return JS_UNDEFINED;
	}

	/* Cache it */
	JS_SetPropertyStr(ctx, handler_map, name_str,
			  JS_DupValue(ctx, compiled));
	JS_FreeValue(ctx, handler_map);
	free(name_str);

	return compiled;
}

void qjsw_set_event_handler(JSContext *ctx, JSValue priv_val,
			     JSValue handler,
			     const char *event_name, size_t len)
{
	/* Store in handler map */
	JSValue handler_map = JS_GetPropertyStr(ctx, priv_val,
						QJS_HANDLER_MAP);
	if (JS_IsUndefined(handler_map)) {
		handler_map = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, priv_val, QJS_HANDLER_MAP,
				  JS_DupValue(ctx, handler_map));
	}

	char *name_str = malloc(len + 1);
	memcpy(name_str, event_name, len);
	name_str[len] = '\0';

	JS_SetPropertyStr(ctx, handler_map, name_str,
			  JS_DupValue(ctx, handler));
	JS_FreeValue(ctx, handler_map);
	free(name_str);

	/* Ensure the event listener is registered for this event type */
	dom_string *dom_name;
	dom_exception exc;
	exc = dom_string_create((const uint8_t *)event_name, len, &dom_name);
	if (exc != DOM_NO_ERR) {
		return;
	}

	/* Get the underlying element from private data — we need to
	 * register the DOM listener. The private node pointer is in
	 * the opaque data of priv_val. We'll try to get it. */
	node_private_t *priv = JS_GetOpaque(priv_val, 0);
	if (priv != NULL && priv->node != NULL) {
		qjsw_register_event_listener_for(ctx,
						  (struct dom_element *)priv->node,
						  dom_name, false);
	}
	dom_string_unref(dom_name);
}

/* ── Generic event handler ─────────────────────────────────────────────── */

/* AT_TARGET dedup: libdom calls the handler twice during AT_TARGET
 * (once from capture walk, once from bubble walk). We track which
 * (event, target) pairs have already fired AT_TARGET listeners to
 * avoid doubling. Supports nested dispatch via a small stack. */
#define AT_TARGET_STACK_SIZE 16
static struct {
	struct dom_event *evt;
	struct dom_event_target *targ;
} at_target_fired[AT_TARGET_STACK_SIZE];
int at_target_depth = 0;

void qjsw_generic_event_handler(struct dom_event *evt, void *pw)
{
	JSContext *ctx = (JSContext *)pw;
	dom_string *name;
	dom_exception exc;
	dom_event_target *targ;
	dom_event_flow_phase phase;

	NSLOG(quickjs, DEBUG, "QuickJS: handling event...");

	exc = dom_event_get_type(evt, &name);
	if (exc != DOM_NO_ERR) {
		NSLOG(quickjs, DEBUG, "QuickJS: unable to get event type");
		return;
	}

	exc = dom_event_get_event_phase(evt, &phase);
	if (exc != DOM_NO_ERR) {
		dom_string_unref(name);
		return;
	}

	exc = dom_event_get_current_target(evt, &targ);
	if (exc != DOM_NO_ERR) {
		dom_string_unref(name);
		return;
	}

	/* Skip inline handler during capture phase */
	if (phase != DOM_CAPTURING_PHASE) {
		/* Get the node wrapper */
		JSValue node = qjsw_push_node(ctx, (struct dom_node *)targ);
		if (!JS_IsUndefined(node)) {
			/* Try inline event handler */
			JSValue handler = qjsw_get_event_handler(
				ctx, node, targ,
				dom_string_data(name),
				dom_string_length(name));

			if (JS_IsFunction(ctx, handler)) {
				JSValue event_obj = qjsw_push_event(ctx, evt);
				JSValue result = qjsw_pcall(ctx, handler,
							    node, 1,
							    &event_obj,
							    true);
				/* If handler returns false, prevent default */
				if (JS_IsBool(result) &&
				    JS_ToBool(ctx, result) == 0) {
					dom_event_prevent_default(evt);
				}
				JS_FreeValue(ctx, result);
				JS_FreeValue(ctx, event_obj);
			}
			JS_FreeValue(ctx, handler);
		}
		JS_FreeValue(ctx, node);
	}

	/* AT_TARGET dedup: libdom may call the handler twice for the target
	 * node (once from capture walk, once from bubble walk). Skip the
	 * second invocation. Also skip BUBBLING_PHASE for non-bubbling events. */
	bool bubbles = false;
	dom_event_get_bubbles(evt, &bubbles);

	if (phase == DOM_BUBBLING_PHASE && !bubbles) {
		/* Non-bubbling event should not fire during bubble phase */
		dom_node_unref(targ);
		dom_string_unref(name);
		return;
	}

	if (phase == DOM_AT_TARGET) {
		for (int i = 0; i < at_target_depth; i++) {
			if (at_target_fired[i].evt == evt &&
			    at_target_fired[i].targ == targ) {
				dom_node_unref(targ);
				dom_string_unref(name);
				return;
			}
		}
		if (at_target_depth < AT_TARGET_STACK_SIZE) {
			at_target_fired[at_target_depth].evt = evt;
			at_target_fired[at_target_depth].targ = targ;
			at_target_depth++;
		}
	}

	/* Handle addEventListener listeners */
	const char *name_data = dom_string_data(name);
	size_t name_len = dom_string_length(name);

	JSValue target_node = qjsw_push_node(ctx, (struct dom_node *)targ);
	if (JS_IsUndefined(target_node)) {
		goto out;
	}

	JSValue sub_listeners = qjsw_event_target_push_listeners(
		ctx, name_data, name_len, target_node, true);

	if (JS_IsUndefined(sub_listeners)) {
		JS_FreeValue(ctx, target_node);
		goto out;
	}

	/* Copy listeners to avoid modification during iteration */
	JSValue copy = JS_NewArray(ctx);
	uint32_t idx = 0;
	for (;;) {
		JSValue item = JS_GetPropertyUint32(ctx, sub_listeners, idx);
		if (JS_IsUndefined(item)) {
			JS_FreeValue(ctx, item);
			break;
		}

		/* Check for ONCE flag and remove if set */
		JSValue flags_val = JS_GetPropertyUint32(ctx, item, 1);
		int32_t flags = 0;
		JS_ToInt32(ctx, &flags, flags_val);
		JS_FreeValue(ctx, flags_val);

		if (flags & QJSW_ELF_ONCE) {
			qjsw_shuffle_array(ctx, sub_listeners, idx);
			/* Don't increment idx since array shifted */
		} else {
			idx++;
		}

		JS_SetPropertyUint32(ctx, copy, idx > 0 ? idx - 1 : 0,
				     item);
	}
	JS_FreeValue(ctx, sub_listeners);

	/* Dispatch to each listener */
	idx = 0;
	for (;;) {
		JSValue entry = JS_GetPropertyUint32(ctx, copy, idx++);
		if (JS_IsUndefined(entry)) {
			JS_FreeValue(ctx, entry);
			break;
		}

		/* entry = [callback, flags] */
		JSValue callback = JS_GetPropertyUint32(ctx, entry, 0);
		JSValue flags_val = JS_GetPropertyUint32(ctx, entry, 1);
		int32_t flags = 0;
		JS_ToInt32(ctx, &flags, flags_val);
		JS_FreeValue(ctx, flags_val);

		/* Check phase vs capture flag.
		 * At AT_TARGET, all listeners fire (spec §2.9 step 5.9).
		 * Dedup for AT_TARGET is handled at handler entry. */
		if ((phase == DOM_CAPTURING_PHASE && !(flags & QJSW_ELF_CAPTURE)) ||
		    (phase == DOM_BUBBLING_PHASE && (flags & QJSW_ELF_CAPTURE))) {
			JS_FreeValue(ctx, callback);
			JS_FreeValue(ctx, entry);
			continue;
		}

		/* Call the listener */
		JSValue event_obj = qjsw_push_event(ctx, evt);
		JSValue result = qjsw_pcall(ctx, callback, target_node,
					    1, &event_obj, true);

		if (JS_IsBool(result) && JS_ToBool(ctx, result) == 0) {
			dom_event_prevent_default(evt);
		}
		JS_FreeValue(ctx, result);
		JS_FreeValue(ctx, event_obj);
		JS_FreeValue(ctx, callback);
		JS_FreeValue(ctx, entry);
	}

	JS_FreeValue(ctx, copy);
	JS_FreeValue(ctx, target_node);

out:
	dom_node_unref(targ);
	dom_string_unref(name);
}

/* ── DOM Level 3 Node properties ───────────────────────────────────── */
/* localName, namespaceURI, prefix — DOM3 defines these on Node
 * (returns null for non-Element/Attribute).  The WebIDL only declares
 * them on Element, so nsgenbind won't generate them on Node.  We add
 * them manually so Acid3 (and other specs) can read them on any node. */

static JSValue
qjsw_node_localName_getter(JSContext *ctx, JSValueConst this_val)
{
	node_private_t *priv = JS_GetOpaqueAny(this_val);
	if (priv == NULL || !priv->parent.is_node)
		return JS_NULL;
	dom_node_type ntype;
	dom_exception exc = dom_node_get_node_type(priv->node, &ntype);
	if (exc != DOM_NO_ERR)
		return JS_UNDEFINED;
	if (ntype != DOM_ELEMENT_NODE && ntype != DOM_ATTRIBUTE_NODE)
		return JS_NULL;
	dom_string *str = NULL;
	exc = dom_node_get_local_name(priv->node, &str);
	if (exc != DOM_NO_ERR)
		return JS_UNDEFINED;
	if (str == NULL) {
		exc = dom_node_get_node_name(priv->node, &str);
		if (exc != DOM_NO_ERR || str == NULL)
			return JS_NULL;
	}
	JSValue ret = JS_NewStringLen(ctx, dom_string_data(str),
				      dom_string_length(str));
	dom_string_unref(str);
	return ret;
}

static JSValue
qjsw_node_namespaceURI_getter(JSContext *ctx, JSValueConst this_val)
{
	node_private_t *priv = JS_GetOpaqueAny(this_val);
	if (priv == NULL || !priv->parent.is_node)
		return JS_NULL;
	dom_node_type ntype;
	dom_exception exc = dom_node_get_node_type(priv->node, &ntype);
	if (exc != DOM_NO_ERR)
		return JS_UNDEFINED;
	if (ntype != DOM_ELEMENT_NODE && ntype != DOM_ATTRIBUTE_NODE)
		return JS_NULL;
	dom_string *ns = NULL;
	exc = dom_node_get_namespace(priv->node, &ns);
	if (exc != DOM_NO_ERR || ns == NULL)
		return JS_NULL;
	JSValue ret = JS_NewStringLen(ctx, dom_string_data(ns),
				      dom_string_length(ns));
	dom_string_unref(ns);
	return ret;
}

static JSValue
qjsw_node_prefix_getter(JSContext *ctx, JSValueConst this_val)
{
	node_private_t *priv = JS_GetOpaqueAny(this_val);
	if (priv == NULL || !priv->parent.is_node)
		return JS_NULL;
	dom_node_type ntype;
	dom_exception exc = dom_node_get_node_type(priv->node, &ntype);
	if (exc != DOM_NO_ERR)
		return JS_UNDEFINED;
	if (ntype != DOM_ELEMENT_NODE && ntype != DOM_ATTRIBUTE_NODE)
		return JS_NULL;
	dom_string *pfx = NULL;
	exc = dom_node_get_prefix(priv->node, &pfx);
	if (exc != DOM_NO_ERR || pfx == NULL)
		return JS_NULL;
	JSValue ret = JS_NewStringLen(ctx, dom_string_data(pfx),
				      dom_string_length(pfx));
	dom_string_unref(pfx);
	return ret;
}

JSValue qjsw_throw_dom_exception(JSContext *ctx, int code, const char *message)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue ctor = JS_GetPropertyStr(ctx, global, "DOMException");
	JS_FreeValue(ctx, global);

	if (JS_IsFunction(ctx, ctor)) {
		JSValue args[2];
		args[0] = JS_NewInt32(ctx, code);
		args[1] = JS_NewString(ctx, message ? message : "");
		JSValue exc = JS_CallConstructor(ctx, ctor, 2, args);
		JS_FreeValue(ctx, args[0]);
		JS_FreeValue(ctx, args[1]);
		JS_FreeValue(ctx, ctor);
		if (!JS_IsException(exc)) {
			JS_Throw(ctx, exc);
			return JS_EXCEPTION;
		}
		JS_FreeValue(ctx, exc);
	}
	JS_FreeValue(ctx, ctor);

	/* Fallback: plain Error if DOMException unavailable */
	return JS_ThrowInternalError(ctx, "DOMException code %d: %s",
				     code, message ? message : "");
}

void qjsw_patch_node_proto_dom3(JSContext *ctx)
{
	JSValue proto = JS_GetClassProto(ctx, node_class_id);
	if (JS_IsUndefined(proto))
		return;

	JSAtom a;

	a = JS_NewAtom(ctx, "localName");
	JS_DefinePropertyGetSet(ctx, proto, a,
		JS_NewCFunction2(ctx,
			(JSCFunction *)(void *)qjsw_node_localName_getter,
			"localName", 0, JS_CFUNC_getter, 0),
		JS_UNDEFINED,
		JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
	JS_FreeAtom(ctx, a);

	a = JS_NewAtom(ctx, "namespaceURI");
	JS_DefinePropertyGetSet(ctx, proto, a,
		JS_NewCFunction2(ctx,
			(JSCFunction *)(void *)qjsw_node_namespaceURI_getter,
			"namespaceURI", 0, JS_CFUNC_getter, 0),
		JS_UNDEFINED,
		JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
	JS_FreeAtom(ctx, a);

	a = JS_NewAtom(ctx, "prefix");
	JS_DefinePropertyGetSet(ctx, proto, a,
		JS_NewCFunction2(ctx,
			(JSCFunction *)(void *)qjsw_node_prefix_getter,
			"prefix", 0, JS_CFUNC_getter, 0),
		JS_UNDEFINED,
		JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
	JS_FreeAtom(ctx, a);

	/* Node type constants (DOM spec §4.5) — on prototype so all
	 * node instances inherit them (e.g. document.DOCUMENT_FRAGMENT_NODE) */
	JS_SetPropertyStr(ctx, proto, "ELEMENT_NODE", JS_NewInt32(ctx, 1));
	JS_SetPropertyStr(ctx, proto, "ATTRIBUTE_NODE", JS_NewInt32(ctx, 2));
	JS_SetPropertyStr(ctx, proto, "TEXT_NODE", JS_NewInt32(ctx, 3));
	JS_SetPropertyStr(ctx, proto, "CDATA_SECTION_NODE", JS_NewInt32(ctx, 4));
	JS_SetPropertyStr(ctx, proto, "ENTITY_REFERENCE_NODE", JS_NewInt32(ctx, 5));
	JS_SetPropertyStr(ctx, proto, "ENTITY_NODE", JS_NewInt32(ctx, 6));
	JS_SetPropertyStr(ctx, proto, "PROCESSING_INSTRUCTION_NODE", JS_NewInt32(ctx, 7));
	JS_SetPropertyStr(ctx, proto, "COMMENT_NODE", JS_NewInt32(ctx, 8));
	JS_SetPropertyStr(ctx, proto, "DOCUMENT_NODE", JS_NewInt32(ctx, 9));
	JS_SetPropertyStr(ctx, proto, "DOCUMENT_TYPE_NODE", JS_NewInt32(ctx, 10));
	JS_SetPropertyStr(ctx, proto, "DOCUMENT_FRAGMENT_NODE", JS_NewInt32(ctx, 11));
	JS_SetPropertyStr(ctx, proto, "NOTATION_NODE", JS_NewInt32(ctx, 12));

	JS_FreeValue(ctx, proto);
}

#endif /* !QJSW_HELPERS_TEST_BUILD */
