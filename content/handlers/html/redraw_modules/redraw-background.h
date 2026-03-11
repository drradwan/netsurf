/*
 * redraw-background.h — Background rendering
 * Auto-generated from redraw.c refactor.
 */

#ifndef NETSURF_HTML_REDRAW_BACKGROUND_H
#define NETSURF_HTML_REDRAW_BACKGROUND_H

#include <stdbool.h>
#include "netsurf/plotters.h"
#include "css/utils.h"
#include "html/box.h"
#include "html/private.h"
#include "html/form_internal.h"

bool html_redraw_box_has_background(struct box *box);
struct box *html_redraw_find_bg_box(struct box *box);
bool html_redraw_background(int x, int y, struct box *box, float scale,
        const struct rect *clip, colour *background_colour,
        struct box *background,
        const css_unit_ctx *unit_len_ctx,
        const struct redraw_context *ctx,
        const html_content *html);
bool html_redraw_inline_background(int x, int y, struct box *box,
        float scale, const struct rect *clip, struct rect b,
        bool first, bool last, colour *background_colour,
        const css_unit_ctx *unit_len_ctx, const html_content *html,
        const struct redraw_context *ctx);

#endif /* NETSURF_HTML_REDRAW_BACKGROUND_H */
