#
# NetSurf javascript/quickjs source file inclusion
#
# Included by javascript/Makefile when NETSURF_USE_QUICKJS=YES
#

# Dependency: qjscore.c needs the embedded JS byte arrays
content/handlers/javascript/quickjs/qjscore.c: \
	$(OBJROOT)/quickjs/polyfill.js.inc \
	$(OBJROOT)/quickjs/generics.js.inc

# Generate polyfill.js.inc from the QuickJS-specific polyfill
$(OBJROOT)/quickjs/polyfill.js.inc: content/handlers/javascript/quickjs/polyfill.js
	$(Q)$(MKDIR) -p $(OBJROOT)/quickjs
	$(VQ)echo "     XXD: $<"
	$(Q)xxd -i -n polyfill_js $< $@

# Generate generics.js.inc from the QuickJS-specific generics
$(OBJROOT)/quickjs/generics.js.inc: content/handlers/javascript/quickjs/generics.js
	$(Q)$(MKDIR) -p $(OBJROOT)/quickjs
	$(VQ)echo "     XXD: $<"
	$(Q)xxd -i -n generics_js $< $@

# QuickJS binding files
QJS_BINDINGS := $(wildcard content/handlers/javascript/quickjs/bnd/*.bnd)

# ensure genbind generates debugging files
GBFLAGS+=-D

# Run nsgenbind with QuickJS backend to generate binding C files
$(OBJROOT)/quickjs/binding.h $(OBJROOT)/quickjs/Makefile: content/handlers/javascript/quickjs/bnd/netsurf.bnd $(QJS_BINDINGS)
	$(Q)$(MKDIR) -p $(OBJROOT)/quickjs
	$(VQ)echo " GENBIND: $< (quickjs)"
	$(Q)nsgenbind $(GBFLAGS) -t quickjs -I content/handlers/javascript/WebIDL $< $(OBJROOT)/quickjs

ifeq ($(filter $(MAKECMDGOALS),clean test coverage),)
-include $(OBJROOT)/quickjs/Makefile
endif

S_JAVASCRIPT_BINDING:=$(addprefix $(OBJROOT)/quickjs/,$(NSGENBIND_SOURCES))

$(S_JAVASCRIPT_BINDING): $(QJS_BINDINGS)

S_JAVASCRIPT += content.c quickjs/qjscore.c quickjs/qjsw_helpers.c

CFLAGS  += -I$(PREFIX)/include -Icontent/handlers/javascript/quickjs
LDFLAGS += -L$(PREFIX)/lib -lquickjs
