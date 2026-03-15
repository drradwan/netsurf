/*
 * redraw-background.c — Background rendering
 * Auto-generated from redraw.c refactor.
 */

#include "utils/config.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dom/dom.h>

#include "utils/log.h"
#include "utils/messages.h"
#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/corestrings.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"
#include "netsurf/layout.h"
#include "content/content.h"
#include "content/content_protected.h"
#include "content/textsearch.h"
#include "css/utils.h"
#include "desktop/selection.h"
#include "desktop/print.h"
#include "desktop/scrollbar.h"
#include "desktop/textarea.h"
#include "desktop/gui_internal.h"

#include "html/box.h"
#include "html/box_inspect.h"
#include "html/box_manipulate.h"
#include "html/font.h"
#include "html/form_internal.h"
#include "html/private.h"
#include "html/layout.h"

#include "html/redraw_modules/redraw-background.h"

/**
 * Determine if a box has a background that needs drawing
 *
 * \param box  Box to consider
 * \return True if box has a background, false otherwise.
 */
bool html_redraw_box_has_background(struct box *box)
{
	if (box->background != NULL)
		return true;

	if (box->style != NULL) {
		css_color colour;

		css_computed_background_color(box->style, &colour);

		if (nscss_color_is_transparent(colour) == false)
			return true;
	}

	return false;
}

/**
 * Find the background box for a box
 *
 * \param box  Box to find background box for
 * \return Pointer to background box, or NULL if there is none
 */
struct box *html_redraw_find_bg_box(struct box *box)
{
	/* Thanks to backwards compatibility, CSS defines the following:
	 *
	 * + If the box is for the root element and it has a background,
	 *   use that (and then process the body box with no special case)
	 * + If the box is for the root element and it has no background,
	 *   then use the background (if any) from the body element as if
	 *   it were specified on the root. Then, when the box for the body
	 *   element is processed, ignore the background.
	 * + For any other box, just use its own styling.
	 */
	if (box->parent == NULL) {
		/* Root box */
		if (html_redraw_box_has_background(box))
			return box;

		/* No background on root box: consider body box, if any */
		if (box->children != NULL) {
			if (html_redraw_box_has_background(box->children))
				return box->children;
		}
	} else if (box->parent != NULL && box->parent->parent == NULL) {
		/* Body box: only render background if root has its own */
		if (html_redraw_box_has_background(box) &&
				html_redraw_box_has_background(box->parent))
			return box;
	} else {
		/* Any other box */
		if (html_redraw_box_has_background(box))
			return box;
	}

	return NULL;
}

/**
 * Plot background images.
 *
 * The reason for the presence of \a background is the backwards compatibility
 * mess that is backgrounds on &lt;body&gt;. The background will be drawn relative
 * to \a box, using the background information contained within \a background.
 *
 * \param  x	  coordinate of box
 * \param  y	  coordinate of box
 * \param  box	  box to draw background image of
 * \param  scale  scale for redraw
 * \param  clip   current clip rectangle
 * \param  background_colour  current background colour
 * \param  background  box containing background details (usually \a box)
 * \param  unit_len_ctx  Length conversion context
 * \param  ctx      current redraw context
 * \return true if successful, false otherwise
 */

bool html_redraw_background(int x, int y, struct box *box, float scale,
		const struct rect *clip, colour *background_colour,
		struct box *background,
		const css_unit_ctx *unit_len_ctx,
		const struct redraw_context *ctx,
		const html_content *html)
{
	bool repeat_x = false;
	bool repeat_y = false;
	bool plot_colour = true;
	bool plot_content;
	bool clip_to_children = false;
	struct box *clip_box = box;
	int ox = x, oy = y;
	int width, height;
	css_fixed hpos = 0, vpos = 0;
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
	struct box *parent;
	struct rect r = *clip;
	css_color bgcol;
	plot_style_t pstyle_fill_bg = {
		.fill_type = PLOT_OP_TYPE_SOLID,
		.fill_colour = *background_colour,
	};
	nserror res;

	if (ctx->background_images == false)
		return true;

	plot_content = (background->background != NULL);

	if (plot_content) {
		bool bg_fixed = (css_computed_background_attachment(
				background->style) ==
				CSS_BACKGROUND_ATTACHMENT_FIXED);

		if (bg_fixed && html != NULL) {
			/* background-attachment:fixed — position image
			 * relative to the viewport, not the box.
			 * redraw_offset_x/y is the content-to-screen
			 * translation set in html_redraw(). */
			x = html->redraw_offset_x;
			y = html->redraw_offset_y;
			width = html->base.available_width;
			height = html->base.height;
		} else if (!box->parent) {
			/* Root element, special case:
			 * background origin calc. is based on margin box */
			x -= box->margin[LEFT] * scale;
			y -= box->margin[TOP] * scale;
			width = box->margin[LEFT] + box->padding[LEFT] +
					box->width + box->padding[RIGHT] +
					box->margin[RIGHT];
			height = box->margin[TOP] + box->padding[TOP] +
					box->height + box->padding[BOTTOM] +
					box->margin[BOTTOM];
		} else {
			width = box->padding[LEFT] + box->width +
					box->padding[RIGHT];
			height = box->padding[TOP] + box->height +
					box->padding[BOTTOM];
		}
		/* handle background-repeat */
		switch (css_computed_background_repeat(background->style)) {
		case CSS_BACKGROUND_REPEAT_REPEAT:
			repeat_x = repeat_y = true;
			/* optimisation: only plot the colour if
			 * bitmap is not opaque */
			plot_colour = !content_get_opaque(background->background);
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_X:
			repeat_x = true;
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_Y:
			repeat_y = true;
			break;

		case CSS_BACKGROUND_REPEAT_NO_REPEAT:
			break;

		default:
			break;
		}

		/* handle background-position */
		css_computed_background_position(background->style,
				&hpos, &hunit, &vpos, &vunit);
		if (hunit == CSS_UNIT_PCT) {
			x += (width -
				content_get_width(background->background)) *
				scale * FIXTOFLT(hpos) / 100.;
		} else {
			x += (int) (FIXTOFLT(css_unit_len2device_px(
					background->style, unit_len_ctx,
					hpos, hunit)) * scale);
		}
		if (vunit == CSS_UNIT_PCT) {
			y += (height -
				content_get_height(background->background)) *
				scale * FIXTOFLT(vpos) / 100.;
		} else {
			y += (int) (FIXTOFLT(css_unit_len2device_px(
					background->style, unit_len_ctx,
					vpos, vunit)) * scale);
		}
	}

	/* special case for table rows as their background needs
	 * to be clipped to all the cells */
	if (box->type == BOX_TABLE_ROW) {
		css_fixed h = 0, v = 0;
		css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;

		for (parent = box->parent;
			((parent) && (parent->type != BOX_TABLE));
				parent = parent->parent);
		assert(parent && (parent->style));

		css_computed_border_spacing(parent->style, &h, &hu, &v, &vu);

		clip_to_children = (h > 0) || (v > 0);

		if (clip_to_children)
			clip_box = box->children;
	}

	for (; clip_box; clip_box = clip_box->next) {
		/* clip to child boxes if needed */
		if (clip_to_children) {
			assert(clip_box->type == BOX_TABLE_CELL);

			/* update clip.* to the child cell */
			r.x0 = ox + (clip_box->x * scale);
			r.y0 = oy + (clip_box->y * scale);
			r.x1 = r.x0 + (clip_box->padding[LEFT] +
					clip_box->width +
					clip_box->padding[RIGHT]) * scale;
			r.y1 = r.y0 + (clip_box->padding[TOP] +
					clip_box->height +
					clip_box->padding[BOTTOM]) * scale;

			if (r.x0 < clip->x0) r.x0 = clip->x0;
			if (r.y0 < clip->y0) r.y0 = clip->y0;
			if (r.x1 > clip->x1) r.x1 = clip->x1;
			if (r.y1 > clip->y1) r.y1 = clip->y1;

			css_computed_background_color(clip_box->style, &bgcol);

			/* <td> attributes override <tr> */
			/* if the background content is opaque there
			 * is no need to plot underneath it.
			 */
			if ((r.x0 >= r.x1) ||
			    (r.y0 >= r.y1) ||
			    (nscss_color_is_transparent(bgcol) == false) ||
			    ((clip_box->background != NULL) &&
			     content_get_opaque(clip_box->background)))
				continue;
		}

		/* plot the background colour */
		css_computed_background_color(background->style, &bgcol);

		if (nscss_color_is_transparent(bgcol) == false) {
			*background_colour = nscss_color_to_ns(bgcol);
			pstyle_fill_bg.fill_colour = *background_colour;
			if (plot_colour) {
				res = ctx->plot->rectangle(ctx, &pstyle_fill_bg, &r);
				if (res != NSERROR_OK) {
					return false;
				}
			}
		}
		/* and plot the image */
		if (plot_content) {
			int bg_iw = content_get_width(background->background);
			int bg_ih = content_get_height(background->background);
			int bg_w = bg_iw;
			int bg_h = bg_ih;

			/* Apply background-size */
			{
				css_fixed size_w = 0, size_h = 0;
				css_unit unit_w = CSS_UNIT_PX;
				css_unit unit_h = CSS_UNIT_PX;
				uint8_t bg_size_type;

				bg_size_type = css_computed_background_size(
						background->style,
						&size_w, &unit_w,
						&size_h, &unit_h);

				switch (bg_size_type) {
				case CSS_BACKGROUND_SIZE_COVER:
					if (bg_iw > 0 && bg_ih > 0) {
						float sx = (float)width / bg_iw;
						float sy = (float)height / bg_ih;
						float s = (sx > sy) ? sx : sy;
						bg_w = (int)(bg_iw * s);
						bg_h = (int)(bg_ih * s);
					}
					break;
				case CSS_BACKGROUND_SIZE_CONTAIN:
					if (bg_iw > 0 && bg_ih > 0) {
						float sx = (float)width / bg_iw;
						float sy = (float)height / bg_ih;
						float s = (sx < sy) ? sx : sy;
						bg_w = (int)(bg_iw * s);
						bg_h = (int)(bg_ih * s);
					}
					break;
				case CSS_BACKGROUND_SIZE_SET:
				{
					bool w_auto = (unit_w == 0);
					bool h_auto = (unit_h == 0);

					if (!w_auto) {
						if (unit_w == CSS_UNIT_PCT) {
							bg_w = width *
								FIXTOFLT(size_w) / 100.;
						} else {
							bg_w = (int)FIXTOFLT(
								css_unit_len2device_px(
								background->style,
								unit_len_ctx,
								size_w, unit_w));
						}
					}

					if (!h_auto) {
						if (unit_h == CSS_UNIT_PCT) {
							bg_h = height *
								FIXTOFLT(size_h) / 100.;
						} else {
							bg_h = (int)FIXTOFLT(
								css_unit_len2device_px(
								background->style,
								unit_len_ctx,
								size_h, unit_h));
						}
					}

					if (w_auto && !h_auto && bg_ih > 0) {
						bg_w = bg_iw * bg_h / bg_ih;
					} else if (h_auto && !w_auto && bg_iw > 0) {
						bg_h = bg_ih * bg_w / bg_iw;
					}
					break;
				}
				case CSS_BACKGROUND_SIZE_AUTO:
				default:
					break;
				}
			}

			width = bg_w;
			height = bg_h;

			/* ensure clip area only as large as required */
			if (!repeat_x) {
				if (r.x0 < x)
					r.x0 = x;
				if (r.x1 > x + width * scale)
					r.x1 = x + width * scale;
			}
			if (!repeat_y) {
				if (r.y0 < y)
					r.y0 = y;
				if (r.y1 > y + height * scale)
					r.y1 = y + height * scale;
			}
			/* valid clipping rectangles only */
			if ((r.x0 < r.x1) && (r.y0 < r.y1)) {
				struct content_redraw_data bg_data;

				res = ctx->plot->clip(ctx, &r);
				if (res != NSERROR_OK) {
					return false;
				}

				bg_data.x = x;
				bg_data.y = y;
				bg_data.width = ceilf(width * scale);
				bg_data.height = ceilf(height * scale);
				bg_data.background_colour = *background_colour;
				bg_data.scale = scale;
				bg_data.repeat_x = repeat_x;
				bg_data.repeat_y = repeat_y;

				/* We just continue if redraw fails */
				content_redraw(background->background,
						&bg_data, &r, ctx);
			}
		}

		/* only <tr> rows being clipped to child boxes loop */
		if (!clip_to_children)
			return true;
	}
	return true;
}

/**
 * Plot an inline's background and/or background image.
 *
 * \param  x	  coordinate of box
 * \param  y	  coordinate of box
 * \param  box	  BOX_INLINE which created the background
 * \param  scale  scale for redraw
 * \param  clip	  coordinates of clip rectangle
 * \param  b	  coordinates of border edge rectangle
 * \param  first  true if this is the first rectangle associated with the inline
 * \param  last   true if this is the last rectangle associated with the inline
 * \param  background_colour  updated to current background colour if plotted
 * \param  unit_len_ctx  Length conversion context
 * \param  ctx      current redraw context
 * \return true if successful, false otherwise
 */

bool html_redraw_inline_background(int x, int y, struct box *box,
		float scale, const struct rect *clip, struct rect b,
		bool first, bool last, colour *background_colour,
		const css_unit_ctx *unit_len_ctx,
		const html_content *html,
		const struct redraw_context *ctx)
{
	struct rect r = *clip;
	bool repeat_x = false;
	bool repeat_y = false;
	bool plot_colour = true;
	bool plot_content;
	css_fixed hpos = 0, vpos = 0;
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
	css_color bgcol;
	plot_style_t pstyle_fill_bg = {
		.fill_type = PLOT_OP_TYPE_SOLID,
		.fill_colour = *background_colour,
	};
	nserror res;

	plot_content = (box->background != NULL);

	/* background-attachment:fixed — position relative to viewport */
	if (html != NULL) {
		bool bg_fixed = (css_computed_background_attachment(box->style) ==
				CSS_BACKGROUND_ATTACHMENT_FIXED);
		if (bg_fixed) {
			x = html->redraw_offset_x;
			y = html->redraw_offset_y;
		}
	}

	if (html_redraw_printing && nsoption_bool(remove_backgrounds))
		return true;

	if (plot_content) {
		/* handle background-repeat */
		switch (css_computed_background_repeat(box->style)) {
		case CSS_BACKGROUND_REPEAT_REPEAT:
			repeat_x = repeat_y = true;
			/* optimisation: only plot the colour if
			 * bitmap is not opaque
			 */
			plot_colour = !content_get_opaque(box->background);
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_X:
			repeat_x = true;
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_Y:
			repeat_y = true;
			break;

		case CSS_BACKGROUND_REPEAT_NO_REPEAT:
			break;

		default:
			break;
		}

		/* handle background-position */
		css_computed_background_position(box->style,
				&hpos, &hunit, &vpos, &vunit);
		if (hunit == CSS_UNIT_PCT) {
			x += (b.x1 - b.x0 -
					content_get_width(box->background) *
					scale) * FIXTOFLT(hpos) / 100.;

			if (!repeat_x && ((hpos < 2 && !first) ||
					(hpos > 98 && !last))){
				plot_content = false;
			}
		} else {
			x += (int) (FIXTOFLT(css_unit_len2device_px(
					box->style, unit_len_ctx,
					hpos, hunit)) * scale);
		}

		if (vunit == CSS_UNIT_PCT) {
			y += (b.y1 - b.y0 -
					content_get_height(box->background) *
					scale) * FIXTOFLT(vpos) / 100.;
		} else {
			y += (int) (FIXTOFLT(css_unit_len2device_px(
					box->style, unit_len_ctx,
					vpos, vunit)) * scale);
		}

	}

	/* plot the background colour */
	css_computed_background_color(box->style, &bgcol);

	if (nscss_color_is_transparent(bgcol) == false) {
		*background_colour = nscss_color_to_ns(bgcol);
		pstyle_fill_bg.fill_colour = *background_colour;

		if (plot_colour) {
			res = ctx->plot->rectangle(ctx, &pstyle_fill_bg, &r);
			if (res != NSERROR_OK) {
				return false;
			}
		}
	}
	/* and plot the image */
	if (plot_content) {
		int bg_iw = content_get_width(box->background);
		int bg_ih = content_get_height(box->background);
		int width = bg_iw;
		int height = bg_ih;
		int box_w = b.x1 - b.x0;
		int box_h = b.y1 - b.y0;

		/* Apply background-size */
		{
			css_fixed size_w = 0, size_h = 0;
			css_unit unit_w = CSS_UNIT_PX;
			css_unit unit_h = CSS_UNIT_PX;
			uint8_t bg_size_type;

			bg_size_type = css_computed_background_size(
					box->style,
					&size_w, &unit_w,
					&size_h, &unit_h);

			switch (bg_size_type) {
			case CSS_BACKGROUND_SIZE_COVER:
				if (bg_iw > 0 && bg_ih > 0) {
					float sx = (float)box_w / (bg_iw * scale);
					float sy = (float)box_h / (bg_ih * scale);
					float s = (sx > sy) ? sx : sy;
					width = (int)(bg_iw * s);
					height = (int)(bg_ih * s);
				}
				break;
			case CSS_BACKGROUND_SIZE_CONTAIN:
				if (bg_iw > 0 && bg_ih > 0) {
					float sx = (float)box_w / (bg_iw * scale);
					float sy = (float)box_h / (bg_ih * scale);
					float s = (sx < sy) ? sx : sy;
					width = (int)(bg_iw * s);
					height = (int)(bg_ih * s);
				}
				break;
			case CSS_BACKGROUND_SIZE_SET:
			{
				bool w_auto = (unit_w == 0);
				bool h_auto = (unit_h == 0);

				if (!w_auto) {
					if (unit_w == CSS_UNIT_PCT) {
						width = box_w *
							FIXTOFLT(size_w) /
							(100. * scale);
					} else {
						width = (int)FIXTOFLT(
							css_unit_len2device_px(
							box->style,
							unit_len_ctx,
							size_w, unit_w));
					}
				}

				if (!h_auto) {
					if (unit_h == CSS_UNIT_PCT) {
						height = box_h *
							FIXTOFLT(size_h) /
							(100. * scale);
					} else {
						height = (int)FIXTOFLT(
							css_unit_len2device_px(
							box->style,
							unit_len_ctx,
							size_h, unit_h));
					}
				}

				if (w_auto && !h_auto && bg_ih > 0) {
					width = bg_iw * height / bg_ih;
				} else if (h_auto && !w_auto && bg_iw > 0) {
					height = bg_ih * width / bg_iw;
				}
				break;
			}
			case CSS_BACKGROUND_SIZE_AUTO:
			default:
				break;
			}
		}

		if (!repeat_x) {
			if (r.x0 < x)
				r.x0 = x;
			if (r.x1 > x + width * scale)
				r.x1 = x + width * scale;
		}
		if (!repeat_y) {
			if (r.y0 < y)
				r.y0 = y;
			if (r.y1 > y + height * scale)
				r.y1 = y + height * scale;
		}
		/* valid clipping rectangles only */
		if ((r.x0 < r.x1) && (r.y0 < r.y1)) {
			struct content_redraw_data bg_data;

			res = ctx->plot->clip(ctx, &r);
			if (res != NSERROR_OK) {
				return false;
			}

			bg_data.x = x;
			bg_data.y = y;
			bg_data.width = ceilf(width * scale);
			bg_data.height = ceilf(height * scale);
			bg_data.background_colour = *background_colour;
			bg_data.scale = scale;
			bg_data.repeat_x = repeat_x;
			bg_data.repeat_y = repeat_y;

			/* We just continue if redraw fails */
			content_redraw(box->background, &bg_data, &r, ctx);
		}
	}

	return true;
}

