/*
 * redraw-text.h — Text rendering
 * Auto-generated from redraw.c refactor.
 */

#ifndef NETSURF_HTML_REDRAW_TEXT_H
#define NETSURF_HTML_REDRAW_TEXT_H

#include <stdbool.h>
#include "netsurf/plotters.h"
#include "css/utils.h"
#include "html/box.h"
#include "html/private.h"
#include "html/form_internal.h"

bool text_redraw(const char *utf8_text, size_t utf8_len, size_t offset,
        int space, const plot_font_style_t *fstyle,
        int x, int y, const struct rect *clip, int height, float scale,
        bool excluded, struct content *c, const struct selection *sel,
        const struct redraw_context *ctx);
bool html_redraw_text_decoration_inline(struct box *box,
        int x, int y, float scale, colour colour, float ratio,
        const struct redraw_context *ctx);
bool html_redraw_text_decoration_block(struct box *box,
        int x, int y, float scale, colour colour, float ratio,
        const struct redraw_context *ctx);
bool html_redraw_text_decoration(struct box *box,
        int x_parent, int y_parent, float scale,
        colour background_colour, const struct redraw_context *ctx);
bool html_redraw_text_box(const html_content *html, struct box *box,
        int x, int y, const struct rect *clip, float scale,
        colour current_background_color,
        const struct redraw_context *ctx);

#endif /* NETSURF_HTML_REDRAW_TEXT_H */
