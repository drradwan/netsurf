/*
 * redraw-forms.h — Form element rendering
 * Auto-generated from redraw.c refactor.
 */

#ifndef NETSURF_HTML_REDRAW_FORMS_H
#define NETSURF_HTML_REDRAW_FORMS_H

#include <stdbool.h>
#include "netsurf/plotters.h"
#include "css/utils.h"
#include "html/box.h"
#include "html/private.h"
#include "html/form_internal.h"

bool html_redraw_checkbox(int x, int y, int width, int height,
        bool selected, const struct redraw_context *ctx);
bool html_redraw_radio(int x, int y, int width, int height,
        bool selected, const struct redraw_context *ctx);
bool html_redraw_file(int x, int y, int width, int height,
        struct box *box, float scale, colour background_colour,
        const css_unit_ctx *unit_len_ctx,
        const struct redraw_context *ctx);

#endif /* NETSURF_HTML_REDRAW_FORMS_H */
