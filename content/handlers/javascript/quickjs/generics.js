/*
 * Generics for QuickJS binding in NetSurf
 *
 * QuickJS-specific fork of duktape/generics.js.
 * Fixes:
 *   - Proxy get/has traps: check for string-encoded numeric keys
 *     (ES2020 Proxy passes all keys as strings, unlike Duktape)
 *   - Formatter.apply(null, result) instead of Formatter.apply(result)
 *     (apply's second arg is the arguments array, not this)
 *
 * The result of this *MUST* be setting a NetSurf object only.
 * That object will then be absorbed into the global object as a hidden
 * object which is used by the rest of the bindings.
 */

var NetSurf = {
    /* The make-proxy call for list-type objects */
    makeListProxy: function(inner) {
    return new Proxy(inner, {
        has: function(target, key) {
        if (!isNaN(key)) {
            var n = Number(key);
            return (n >= 0) && (n < target.length);
        } else if (typeof key === 'symbol') {
            return key in target;
        } else {
            if (target[key] !== undefined) return true;
            if (typeof key === 'string' && typeof target.namedItem === 'function') {
            try { var ni = target.namedItem(key); if (ni) return true; } catch(e) {}
            }
            return false;
        }
        },
        get: function(target, key) {
        if (!isNaN(key)) {
            return target.item(Number(key));
        } else if (typeof key === 'symbol') {
            return target[key];
        } else {
            var v = target[key];
            if (v !== undefined) return v;
            if (typeof key === 'string' && typeof target.namedItem === 'function') {
            try { var ni = target.namedItem(key); if (ni) return ni; } catch(e) {}
            }
            return v;
        }
        },
    });
    },
    /* The make-proxy call for nodemap-type objects */
    makeNodeMapProxy: function(inner) {
    return new Proxy(inner, {
        has: function(target, key) {
        if (!isNaN(key)) {
            var n = Number(key);
            return (n >= 0) && (n < target.length);
        } else {
            return target.getNamedItem(key) || (key in target);
        }
        },
        get: function(target, key) {
        if (!isNaN(key)) {
            return target.item(Number(key));
        } else {
            var attr = target.getNamedItem(key);
            if (attr) {
            return attr;
            }
            return target[key];
        }
        },
    });
    },
    consoleFormatter: function Formatter() {

    if (arguments.length == 0) {
        return new Array("");
    } else if (arguments.length == 1) {
        return new Array(arguments[0].toString());
    }

    var target = arguments[0];
    var current = arguments[1];

    if (typeof target !== "string") {
        return Array.from(arguments);
    }

    var offset = target.search("%");

    if (offset == -1 || offset >= (target.length - 1)) {
        return Array.from(arguments);
    }

    var specifier = target[offset + 1];

    var converted = undefined;

    if (specifier === 's') {
        converted = current.toString();
    } else if (specifier === 'd' || specifier === 'i') {
        converted = parseInt(current, 10).toString();
    } else if (specifier === 'f') {
        converted = parseFloat(current).toString();
    } else if (specifier === 'o') {
        converted = current.toString();
    } else if (specifier === 'O') {
        converted = current.toString();
    }

    var result = new Array();

    if (converted !== undefined) {
        var newtarget = "";
        if (offset > 0) {
        newtarget = target.substring(0, offset);
        }
        newtarget = newtarget + converted;
        if (offset < target.length - 2) {
        newtarget = newtarget + target.substring(offset + 2, target.length);
        }
        result.push(newtarget);
    } else {
        result.push(target);
    }

    var i;
    for (i = 2; i < arguments.length; i++) {
        result.push(arguments[i]);
    }

    if (result[0].search("%") == -1) {
        return result;
    }

    if (result.length === 1) {
        return result;
    }

    return Formatter.apply(null, result);
    }
};
