/*
 * qjsw_helpers.h — QuickJS DOM helper functions
 *
 * Copyright 2026 Senfoni Authors
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Phase 5 of the QuickJS migration (docs/quickjs.md).
 *
 * PURPOSE
 * -------
 * ~20 helper functions used in .bnd cdata blocks.
 * QuickJS uses explicit JSValue handles — these helpers return JSValues
 * directly, making cdata blocks straightforward:
 *
 *   return qjsw_push_node(ctx, node);
 *
 * STASH PROPERTIES
 * ----------------
 * Internal maps live on the global object as named properties:
 *   __NSQJS_NODE_MAP__     — dom_node* → JSValue (node wrappers)
 *   __NSQJS_EVENT_MAP__    — dom_event* → JSValue (event wrappers)
 *   __NSQJS_PROTO_MAP__    — name → prototype
 *   __NSQJS_GENERICS__     — NetSurf generics object
 *   __NSQJS_HANDLER_MAP__  — event handler function cache
 */

#ifndef QJSW_HELPERS_H
#define QJSW_HELPERS_H

#include "quickjs/quickjs.h"

struct dom_node;
struct dom_element;
struct dom_event;
struct dom_event_target;
struct dom_document;
struct dom_string;

typedef enum {
	QJSW_ELF_CAPTURE = 1 << 0,
	QJSW_ELF_PASSIVE = 1 << 1,
	QJSW_ELF_ONCE    = 1 << 2,
	QJSW_ELF_NONE    = 0
} qjsw_event_listener_flags;

/**
 * Wrap a DOM node as a JSValue, with memoisation in __NSQJS_NODE_MAP__.
 *
 * If the node was already wrapped, returns the existing wrapper (ref'd).
 * Otherwise creates a new object with the correct prototype (determined
 * from the node type and HTML element tag type), calls __init, and
 * caches it.
 *
 * Caller owns the returned JSValue.
 */
JSValue qjsw_push_node(JSContext *ctx, struct dom_node *node);

/**
 * Wrap a DOM event as a JSValue, with memoisation in __NSQJS_EVENT_MAP__.
 * Caller owns the returned JSValue.
 */
JSValue qjsw_push_event(JSContext *ctx, struct dom_event *evt);

/**
 * Create a new binding object by prototype name.
 *
 * Looks up the prototype in the class system, creates an object with
 * that prototype, allocates private data, and calls the __init function.
 *
 * proto_name: the class name (e.g. "event", "keyboard_event")
 * argc/argv: arguments passed to __init after the private pointer arg
 *
 * Caller owns the returned JSValue.
 */
JSValue qjsw_create_object(JSContext *ctx, const char *proto_name,
			    int argc, JSValueConst *argv);

/**
 * Call a JS function with error handling and optional timeout reset.
 *
 * Logs exceptions with stack traces. Returns the result JSValue
 * (which may be JS_EXCEPTION on error). Caller owns the result.
 */
JSValue qjsw_pcall(JSContext *ctx, JSValue fn, JSValue this_val,
		    int argc, JSValueConst *argv, bool reset_timeout);

/**
 * Get a generics function by name from the __NSQJS_GENERICS__ stash.
 * Caller owns the returned JSValue.
 */
JSValue qjsw_push_generics(JSContext *ctx, const char *name);

/**
 * Reset generics availability cache.  Call from js_newthread.
 */
void qjsw_reset_generics_state(void);

/**
 * Check if a value is an instance of a named prototype class.
 */
bool qjsw_instanceof(JSContext *ctx, JSValue val, const char *proto_name);

/**
 * Get or create the event listeners sub-array for a given event type
 * on a target node.
 *
 * The listeners structure is stored as a property on the node:
 *   node.__NSQJS_EL_JS_MAP__ = { "click": [ [callback, flags], ... ], ... }
 *
 * If dont_create is true and the sub-array doesn't exist, returns
 * JS_UNDEFINED. Otherwise creates it.
 *
 * type: event type string (e.g. "click")
 * node: the target node JSValue
 *
 * Caller owns the returned JSValue.
 */
JSValue qjsw_event_target_push_listeners(JSContext *ctx,
					  const char *type, size_t type_len,
					  JSValue node, bool dont_create);

/**
 * Register a DOM event listener for a named event on an element.
 *
 * Creates the dom_event_listener and attaches it to the element if
 * not already registered for this event type.
 */
void qjsw_register_event_listener_for(JSContext *ctx,
				       struct dom_element *ele,
				       struct dom_string *name,
				       bool capture);

/**
 * Get the current value of an inline event handler (e.g. onclick="...").
 *
 * Checks the handler cache first; if not found, reads the attribute
 * from the DOM element and compiles it into a function.
 *
 * priv_val: the node's JS wrapper
 * et: the event target (element pointer, or NULL for Window)
 * event_name/len: the event name WITHOUT "on" prefix (e.g. "click")
 *
 * Returns JS_UNDEFINED if no handler exists. Caller owns the result.
 */
JSValue qjsw_get_event_handler(JSContext *ctx, JSValue priv_val,
				struct dom_event_target *et,
				const char *event_name, size_t len);

/**
 * Set an event handler function on a node (e.g. node.onclick = fn).
 *
 * Stores the function in the handler cache and ensures the DOM event
 * listener is registered.
 *
 * priv_val: the node's JS wrapper
 * handler: the function value to set (JS_DupValue'd internally)
 * event_name/len: the event name WITHOUT "on" prefix
 */
void qjsw_set_event_handler(JSContext *ctx, JSValue priv_val,
			     JSValue handler,
			     const char *event_name, size_t len);

/**
 * Remove an element from an array by index, shifting subsequent
 * elements down by one.
 */
void qjsw_shuffle_array(JSContext *ctx, JSValue arr, uint32_t idx);

/**
 * The generic event handler callback — registered with libdom.
 * Dispatches to JS event listeners and inline handlers.
 */
void qjsw_generic_event_handler(struct dom_event *evt, void *pw);

/**
 * Close down a thread's JavaScript state — cleans up node map,
 * event map, handler map, and calls Window closedownThread if present.
 */
void qjsw_closedown_thread(JSContext *ctx);

/**
 * Log the current QuickJS exception (if any) with stack trace.
 */
void qjsw_log_exception(JSContext *ctx);

/**
 * Patch the Node prototype with DOM Level 3 properties (localName,
 * namespaceURI, prefix) that return null for non-Element/Attribute nodes.
 * Must be called after qjsw_create_prototypes().
 */
void qjsw_patch_node_proto_dom3(JSContext *ctx);

/**
 * Throw a DOMException with the given legacy error code.
 * Returns JS_EXCEPTION (suitable for direct return from a binding).
 */
JSValue qjsw_throw_dom_exception(JSContext *ctx, int code, const char *message);

#endif /* QJSW_HELPERS_H */
