/*
 * redraw-box.h — Box tree traversal and rendering
 * Auto-generated from redraw.c refactor.
 */

#ifndef NETSURF_HTML_REDRAW_BOX_H
#define NETSURF_HTML_REDRAW_BOX_H

#include <stdbool.h>
#include "netsurf/plotters.h"
#include "css/utils.h"
#include "html/box.h"
#include "html/private.h"
#include "html/form_internal.h"
#include "html/redraw_modules/redraw-background.h"
#include "html/redraw_modules/redraw-forms.h"
#include "html/redraw_modules/redraw-text.h"

bool html_redraw_box_children(const html_content *html, struct box *box,
        int x_parent, int y_parent,
        const struct rect *clip, float scale,
        colour current_background_color,
        const struct redraw_context *ctx);
bool html_redraw_box(const html_content *html, struct box *box,
        int x_parent, int y_parent,
        const struct rect *clip, float scale,
        colour current_background_color,
        const struct redraw_context *ctx);

#endif /* NETSURF_HTML_REDRAW_BOX_H */
