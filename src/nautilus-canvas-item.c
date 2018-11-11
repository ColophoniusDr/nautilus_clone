/* Nautilus - Canvas item class for canvas container.
 *
 * Copyright (C) 2000 Eazel, Inc
 *
 * Author: Andy Hertzfeld <andy@eazel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <math.h>
#include "nautilus-canvas-item.h"

#include <glib/gi18n.h>

#include "nautilus-canvas-container.h"
#include "nautilus-file-utilities.h"
#include "nautilus-global-preferences.h"
#include "nautilus-canvas-private.h"
#include <eel/eel-art-extensions.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-graphic-effects.h>
#include <eel/eel-string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <atk/atkimage.h>
#include <atk/atkcomponent.h>
#include <atk/atknoopobject.h>
#include <stdio.h>
#include <string.h>

/* gap between bottom of icon and start of text box */
#define LABEL_OFFSET 1
#define LABEL_LINE_SPACING 0

/* Text padding */
#define TEXT_BACK_PADDING_X 4
#define TEXT_BACK_PADDING_Y 1

/* Width of the label, keep in sync with ICON_GRID_WIDTH at nautilus-canvas-container.c */
#define MAX_TEXT_WIDTH_SMALL 116
#define MAX_TEXT_WIDTH_STANDARD 104
#define MAX_TEXT_WIDTH_LARGE 98
#define MAX_TEXT_WIDTH_LARGER 100

/* special text height handling
 * each item has three text height variables:
 *  + text_height: actual height of the displayed (i.e. on-screen) PangoLayout.
 *  + text_height_for_layout: height used in canvas grid layout algorithms.
 *                    “sane amount” of text.
 *   “sane amount“ as of
 *      + hard-coded to three lines in text-below-icon mode.
 *
 *  This layout height is used by grid layout algorithms, even
 *  though the actually displayed and/or requested text size may be larger
 *  and overlap adjacent icons, if an icon is selected.
 *
 *  + text_height_for_entire_text: height needed to display the entire PangoLayout,
 *    if it wasn't ellipsized.
 */

struct _NautilusCanvasItem
{
    EelCanvasItem parent_instance;

    /* The image, text, font. */
    double x, y;
    GdkTexture *texture;
    char *editable_text;                /* Text that can be modified by a renaming function */
    char *additional_text;              /* Text that cannot be modifed, such as file size, etc. */

    /* Size of the text at current font. */
    int text_dx;
    int text_width;

    /* actual size required for rendering the text to display */
    int text_height;
    /* actual size that would be required for rendering the entire text if it wasn't ellipsized */
    int text_height_for_entire_text;
    /* actual size needed for rendering a “sane amount” of text */
    int text_height_for_layout;

    int editable_text_height;

    /* whether the entire text must always be visible. In that case,
     * text_height_for_layout will always be equal to text_height.
     * Used for the last line of a line-wise icon layout. */
    guint entire_text : 1;

    /* Highlight state. */
    guint is_highlighted_for_selection : 1;
    guint is_highlighted_as_keyboard_focus : 1;
    guint is_highlighted_for_drop : 1;
    guint is_highlighted_for_clipboard : 1;
    guint is_prelit : 1;

    guint bounds_cached : 1;

    guint is_visible : 1;

    /* Cached PangoLayouts. Only used if the icon is visible */
    PangoLayout *editable_text_layout;
    PangoLayout *additional_text_layout;

    /* Cached rectangle in canvas coordinates */
    EelIRect icon_rect;
    EelIRect text_rect;

    EelIRect bounds_cache;
    EelIRect bounds_cache_for_layout;
    EelIRect bounds_cache_for_entire_item;

    GString *text;

    NautilusCanvasIcon *icon;
};

/* Object argument IDs. */
enum
{
    PROP_0,
    PROP_EDITABLE_TEXT,
    PROP_ADDITIONAL_TEXT,
    PROP_HIGHLIGHTED_FOR_SELECTION,
    PROP_HIGHLIGHTED_AS_KEYBOARD_FOCUS,
    PROP_HIGHLIGHTED_FOR_DROP,
    PROP_HIGHLIGHTED_FOR_CLIPBOARD
};

typedef enum
{
    RIGHT_SIDE,
    BOTTOM_SIDE,
    LEFT_SIDE,
    TOP_SIDE
} RectangleSide;

static GType nautilus_canvas_item_accessible_factory_get_type (void);

G_DEFINE_TYPE (NautilusCanvasItem, nautilus_canvas_item, EEL_TYPE_CANVAS_ITEM)

/* private */
static void     get_icon_rectangle (NautilusCanvasItem *item,
                                    EelIRect           *rect);
static PangoLayout *get_label_layout (PangoLayout       **layout,
                                      NautilusCanvasItem *item,
                                      const char         *text);

static void       nautilus_canvas_item_ensure_bounds_up_to_date (NautilusCanvasItem *canvas_item);

/* Object initialization function for the canvas item. */
static void
nautilus_canvas_item_init (NautilusCanvasItem *canvas_item)
{
    nautilus_canvas_item_invalidate_label_size (canvas_item);
}

static void
nautilus_canvas_item_dispose (GObject *object)
{
    NautilusCanvasItem *self;
    EelCanvasItem *item;

    self = NAUTILUS_CANVAS_ITEM (object);
    item = EEL_CANVAS_ITEM (object);

    gtk_widget_set_cursor (GTK_WIDGET (item->canvas), NULL);

    g_clear_object (&self->texture);
    g_clear_object (&self->editable_text_layout);
    g_clear_object (&self->additional_text_layout);

    G_OBJECT_CLASS (nautilus_canvas_item_parent_class)->dispose (object);
}

static void
nautilus_canvas_item_finalize (GObject *object)
{
    NautilusCanvasItem *self;

    self = NAUTILUS_CANVAS_ITEM (object);

    if (self->text != NULL)
    {
        g_string_free (self->text, TRUE);
        self->text = NULL;
    }

    g_clear_pointer (&self->editable_text, g_free);
    g_clear_pointer (&self->additional_text, g_free);

    G_OBJECT_CLASS (nautilus_canvas_item_parent_class)->finalize (object);
}

static void
nautilus_canvas_item_invalidate_bounds_cache (NautilusCanvasItem *item)
{
    item->bounds_cached = FALSE;
}

void
nautilus_canvas_item_invalidate_label_size (NautilusCanvasItem *item)
{
    if (item->editable_text_layout != NULL)
    {
        pango_layout_context_changed (item->editable_text_layout);
    }
    if (item->additional_text_layout != NULL)
    {
        pango_layout_context_changed (item->additional_text_layout);
    }
    nautilus_canvas_item_invalidate_bounds_cache (item);
    item->text_width = -1;
    item->text_height = -1;
    item->text_height_for_layout = -1;
    item->text_height_for_entire_text = -1;
    item->editable_text_height = -1;
}

static void
nautilus_canvas_item_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    NautilusCanvasItem *self;
    AtkObject *accessible;
    gboolean is_rename;

    self = NAUTILUS_CANVAS_ITEM (object);
    accessible = atk_gobject_accessible_for_object (object);

    switch (property_id)
    {
        case PROP_EDITABLE_TEXT:
        {
            if (g_strcmp0 (self->editable_text,
                           g_value_get_string (value)) == 0)
            {
                return;
            }

            is_rename = self->editable_text != NULL;
            g_free (self->editable_text);
            self->editable_text = g_strdup (g_value_get_string (value));
            if (self->text)
            {
                self->text = g_string_assign (self->text, self->editable_text);

                if (is_rename)
                    g_object_notify (G_OBJECT (accessible), "accessible-name");
            }

            nautilus_canvas_item_invalidate_label_size (self);
            if (self->editable_text_layout)
            {
                g_object_unref (self->editable_text_layout);
                self->editable_text_layout = NULL;
            }
        }
        break;

        case PROP_ADDITIONAL_TEXT:
        {
            if (g_strcmp0 (self->additional_text,
                           g_value_get_string (value)) == 0)
            {
                return;
            }

            g_free (self->additional_text);
            self->additional_text = g_strdup (g_value_get_string (value));

            nautilus_canvas_item_invalidate_label_size (self);
            if (self->additional_text_layout)
            {
                g_object_unref (self->additional_text_layout);
                self->additional_text_layout = NULL;
            }
        }
        break;

        case PROP_HIGHLIGHTED_FOR_SELECTION:
        {
            if (!self->is_highlighted_for_selection == !g_value_get_boolean (value))
            {
                return;
            }
            self->is_highlighted_for_selection = g_value_get_boolean (value);
            nautilus_canvas_item_invalidate_label_size (self);

            atk_object_notify_state_change (accessible, ATK_STATE_SELECTED,
                                            self->is_highlighted_for_selection);
        }
        break;

        case PROP_HIGHLIGHTED_AS_KEYBOARD_FOCUS:
        {
            if (!self->is_highlighted_as_keyboard_focus == !g_value_get_boolean (value))
            {
                return;
            }
            self->is_highlighted_as_keyboard_focus = g_value_get_boolean (value);

            atk_object_notify_state_change (accessible, ATK_STATE_FOCUSED,
                                            self->is_highlighted_as_keyboard_focus);
        }
        break;

        case PROP_HIGHLIGHTED_FOR_DROP:
        {
            if (!self->is_highlighted_for_drop == !g_value_get_boolean (value))
            {
                return;
            }
            self->is_highlighted_for_drop = g_value_get_boolean (value);
        }
        break;

        case PROP_HIGHLIGHTED_FOR_CLIPBOARD:
        {
            if (!self->is_highlighted_for_clipboard == !g_value_get_boolean (value))
            {
                return;
            }
            self->is_highlighted_for_clipboard = g_value_get_boolean (value);
        }
        break;

        default:
            g_warning ("nautilus_canvas_item_set_property on unknown argument");
            return;
    }

    eel_canvas_item_request_update (EEL_CANVAS_ITEM (object));
}

/* Get property handler for the canvas item */
static void
nautilus_canvas_item_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    NautilusCanvasItem *self;

    self = NAUTILUS_CANVAS_ITEM (object);

    switch (property_id)
    {
        case PROP_EDITABLE_TEXT:
        {
            g_value_set_string (value, self->editable_text);
        }
        break;

        case PROP_ADDITIONAL_TEXT:
        {
            g_value_set_string (value, self->additional_text);
        }
        break;

        case PROP_HIGHLIGHTED_FOR_SELECTION:
        {
            g_value_set_boolean (value, self->is_highlighted_for_selection);
        }
        break;

        case PROP_HIGHLIGHTED_AS_KEYBOARD_FOCUS:
        {
            g_value_set_boolean (value, self->is_highlighted_as_keyboard_focus);
        }
        break;

        case PROP_HIGHLIGHTED_FOR_DROP:
        {
            g_value_set_boolean (value, self->is_highlighted_for_drop);
        }
        break;

        case PROP_HIGHLIGHTED_FOR_CLIPBOARD:
        {
            g_value_set_boolean (value, self->is_highlighted_for_clipboard);
        }
        break;

        default:
        {
            g_warning ("invalid property %d", property_id);
        }
        break;
    }
}

static void
get_scaled_icon_size (NautilusCanvasItem *item,
                      gint               *out_width,
                      gint               *out_height)
{
    EelCanvas *canvas;
    int scale;
    GdkTexture *texture;
    int width = 0;
    int height = 0;

    if (item == NULL)
    {
        goto finish;
    }

    canvas = EEL_CANVAS_ITEM (item)->canvas;
    scale = gtk_widget_get_scale_factor (GTK_WIDGET (canvas));
    texture = item->texture;

    if (texture != NULL)
    {
        width = gdk_texture_get_width (texture) / scale;
        height = gdk_texture_get_height (texture) / scale;
    }

finish:
    if (out_width != NULL)
    {
        *out_width = width;
    }
    if (out_height != NULL)
    {
        *out_height = height;
    }
}

void
nautilus_canvas_item_set_texture (NautilusCanvasItem *self,
                                  GdkTexture         *texture)
{
    g_return_if_fail (NAUTILUS_IS_CANVAS_ITEM (self));

    if (self->texture == texture)
    {
        return;
    }

    g_clear_object (&self->texture);
    self->texture = texture;

    nautilus_canvas_item_invalidate_bounds_cache (self);
    eel_canvas_item_request_update (EEL_CANVAS_ITEM (self));
}

/* Recomputes the bounding box of a canvas item.
 * This is a generic implementation that could be used for any canvas item
 * class, it has no assumptions about how the item is used.
 */
static void
recompute_bounding_box (NautilusCanvasItem *canvas_item,
                        double              i2w_dx,
                        double              i2w_dy)
{
    /* The bounds stored in the item is the same as what get_bounds
     * returns, except it's in canvas coordinates instead of the item's
     * parent's coordinates.
     */

    EelCanvasItem *item;
    EelDRect bounds_rect;

    item = EEL_CANVAS_ITEM (canvas_item);

    eel_canvas_item_get_bounds (item,
                                &bounds_rect.x0, &bounds_rect.y0,
                                &bounds_rect.x1, &bounds_rect.y1);

    bounds_rect.x0 += i2w_dx;
    bounds_rect.y0 += i2w_dy;
    bounds_rect.x1 += i2w_dx;
    bounds_rect.y1 += i2w_dy;
    eel_canvas_w2c_d (item->canvas,
                      bounds_rect.x0, bounds_rect.y0,
                      &item->x1, &item->y1);
    eel_canvas_w2c_d (item->canvas,
                      bounds_rect.x1, bounds_rect.y1,
                      &item->x2, &item->y2);
}

static EelIRect
compute_text_rectangle (const NautilusCanvasItem      *item,
                        EelIRect                       icon_rectangle,
                        gboolean                       canvas_coords,
                        NautilusCanvasItemBoundsUsage  usage)
{
    EelIRect text_rectangle;
    double pixels_per_unit;
    double text_width, text_height, text_height_for_layout, text_height_for_entire_text, real_text_height;

    pixels_per_unit = EEL_CANVAS_ITEM (item)->canvas->pixels_per_unit;
    if (canvas_coords)
    {
        text_width = item->text_width;
        text_height = item->text_height;
        text_height_for_layout = item->text_height_for_layout;
        text_height_for_entire_text = item->text_height_for_entire_text;
    }
    else
    {
        text_width = item->text_width / pixels_per_unit;
        text_height = item->text_height / pixels_per_unit;
        text_height_for_layout = item->text_height_for_layout / pixels_per_unit;
        text_height_for_entire_text = item->text_height_for_entire_text / pixels_per_unit;
    }

    text_rectangle.x0 = (icon_rectangle.x0 + icon_rectangle.x1) / 2 - (int) text_width / 2;
    text_rectangle.y0 = icon_rectangle.y1;
    text_rectangle.x1 = text_rectangle.x0 + text_width;

    if (usage == BOUNDS_USAGE_FOR_LAYOUT)
    {
        real_text_height = text_height_for_layout;
    }
    else if (usage == BOUNDS_USAGE_FOR_ENTIRE_ITEM)
    {
        real_text_height = text_height_for_entire_text;
    }
    else if (usage == BOUNDS_USAGE_FOR_DISPLAY)
    {
        real_text_height = text_height;
    }
    else
    {
        g_assert_not_reached ();
    }

    text_rectangle.y1 = text_rectangle.y0 + real_text_height + LABEL_OFFSET / pixels_per_unit;

    return text_rectangle;
}

static EelIRect
get_current_canvas_bounds (EelCanvasItem *item)
{
    EelIRect bounds;

    g_assert (EEL_IS_CANVAS_ITEM (item));

    bounds.x0 = item->x1;
    bounds.y0 = item->y1;
    bounds.x1 = item->x2;
    bounds.y1 = item->y2;

    return bounds;
}

void
nautilus_canvas_item_update_bounds (NautilusCanvasItem *item,
                                    double              i2w_dx,
                                    double              i2w_dy)
{
    EelIRect before, after;
    EelCanvasItem *canvas_item;

    canvas_item = EEL_CANVAS_ITEM (item);

    /* Compute new bounds. */
    before = get_current_canvas_bounds (canvas_item);
    recompute_bounding_box (item, i2w_dx, i2w_dy);
    after = get_current_canvas_bounds (canvas_item);

    /* If the bounds didn't change, we are done. */
    if (eel_irect_equal (before, after))
    {
        return;
    }

    /* Update canvas and text rect cache */
    get_icon_rectangle (item, &item->icon_rect);
    item->text_rect = compute_text_rectangle (item, item->icon_rect,
                                                       TRUE, BOUNDS_USAGE_FOR_DISPLAY);

    /* queue a redraw. */
    eel_canvas_request_redraw (canvas_item->canvas,
                               before.x0, before.y0,
                               before.x1 + 1, before.y1 + 1);
}

/* Update handler for the canvas canvas item. */
static void
nautilus_canvas_item_update (EelCanvasItem *item,
                             double         i2w_dx,
                             double         i2w_dy,
                             gint           flags)
{
    nautilus_canvas_item_update_bounds (NAUTILUS_CANVAS_ITEM (item), i2w_dx, i2w_dy);

    eel_canvas_item_request_redraw (EEL_CANVAS_ITEM (item));

    EEL_CANVAS_ITEM_CLASS (nautilus_canvas_item_parent_class)->update (item, i2w_dx, i2w_dy, flags);
}

/* Rendering */
static gboolean
in_single_click_mode (void)
{
    int click_policy;

    click_policy = g_settings_get_enum (nautilus_preferences,
                                        NAUTILUS_PREFERENCES_CLICK_POLICY);

    return click_policy == NAUTILUS_CLICK_POLICY_SINGLE;
}


/* Keep these for a bit while we work on performance of draw_or_measure_label_text. */
/*
 #define PERFORMANCE_TEST_DRAW_DISABLE
 #define PERFORMANCE_TEST_MEASURE_DISABLE
 */

/* This gets the size of the layout from the position of the layout.
 * This means that if the layout is right aligned we get the full width
 * of the layout, not just the width of the text snippet on the right side
 */
static void
layout_get_full_size (PangoLayout *layout,
                      int         *width,
                      int         *height,
                      int         *dx)
{
    PangoRectangle logical_rect;
    int the_width, total_width;

    pango_layout_get_extents (layout, NULL, &logical_rect);
    the_width = (logical_rect.width + PANGO_SCALE / 2) / PANGO_SCALE;
    total_width = (logical_rect.x + logical_rect.width + PANGO_SCALE / 2) / PANGO_SCALE;

    if (width != NULL)
    {
        *width = the_width;
    }

    if (height != NULL)
    {
        *height = (logical_rect.height + PANGO_SCALE / 2) / PANGO_SCALE;
    }

    if (dx != NULL)
    {
        *dx = total_width - the_width;
    }
}

static void
layout_get_size_for_layout (PangoLayout *layout,
                            int          max_layout_line_count,
                            int          height_for_entire_text,
                            int         *height_for_layout)
{
    PangoLayoutIter *iter;
    PangoRectangle logical_rect;
    int i;

    /* only use the first max_layout_line_count lines for the gridded auto layout */
    if (pango_layout_get_line_count (layout) <= max_layout_line_count)
    {
        *height_for_layout = height_for_entire_text;
    }
    else
    {
        *height_for_layout = 0;
        iter = pango_layout_get_iter (layout);
        for (i = 0; i < max_layout_line_count; i++)
        {
            pango_layout_iter_get_line_extents (iter, NULL, &logical_rect);
            *height_for_layout += (logical_rect.height + PANGO_SCALE / 2) / PANGO_SCALE;

            if (!pango_layout_iter_next_line (iter))
            {
                break;
            }

            *height_for_layout += pango_layout_get_spacing (layout);
        }
        pango_layout_iter_free (iter);
    }
}

static double
nautilus_canvas_item_get_max_text_width (NautilusCanvasItem *item)
{
    EelCanvasItem *canvas_item;
    NautilusCanvasContainer *container;
    guint max_text_width;


    canvas_item = EEL_CANVAS_ITEM (item);
    container = NAUTILUS_CANVAS_CONTAINER (canvas_item->canvas);

    switch (nautilus_canvas_container_get_zoom_level (container))
    {
        case NAUTILUS_CANVAS_ZOOM_LEVEL_SMALL:
        {
            max_text_width = MAX_TEXT_WIDTH_SMALL;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_STANDARD:
        {
            max_text_width = MAX_TEXT_WIDTH_STANDARD;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_LARGE:
        {
            max_text_width = MAX_TEXT_WIDTH_LARGE;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_LARGER:
        {
            max_text_width = MAX_TEXT_WIDTH_LARGER;
        }
        break;

        default:
            g_warning ("Zoom level not valid. This may incur in missaligned grid");
            max_text_width = MAX_TEXT_WIDTH_STANDARD;
    }

    return max_text_width * canvas_item->canvas->pixels_per_unit - 2 * TEXT_BACK_PADDING_X;
}

static void
prepare_pango_layout_width (NautilusCanvasItem *item,
                            PangoLayout        *layout)
{
    pango_layout_set_width (layout, floor (nautilus_canvas_item_get_max_text_width (item)) * PANGO_SCALE);
    pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
}

static void
prepare_pango_layout_for_measure_entire_text (NautilusCanvasItem *item,
                                              PangoLayout        *layout)
{
    prepare_pango_layout_width (item, layout);
    pango_layout_set_height (layout, G_MININT);
}

static void
prepare_pango_layout_for_draw (NautilusCanvasItem *item,
                               PangoLayout        *layout)
{
    NautilusCanvasContainer *container;
    gboolean needs_highlight;

    prepare_pango_layout_width (item, layout);

    container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);

    needs_highlight = item->is_highlighted_for_selection || item->is_highlighted_for_drop;

    if (needs_highlight ||
        item->is_highlighted_as_keyboard_focus ||
        item->entire_text)
    {
        /* VOODOO-TODO, cf. compute_text_rectangle() */
        pango_layout_set_height (layout, G_MININT);
    }
    else
    {
        /* TODO? we might save some resources, when the re-layout is not neccessary in case
         * the layout height already fits into max. layout lines. But pango should figure this
         * out itself (which it doesn't ATM).
         */
        pango_layout_set_height (layout,
                                 nautilus_canvas_container_get_max_layout_lines_for_pango (container));
    }
}

static void
measure_label_text (NautilusCanvasItem *item)
{
    NautilusCanvasContainer *container;
    gint editable_height, editable_height_for_layout, editable_height_for_entire_text, editable_width, editable_dx;
    gint additional_height, additional_width, additional_dx;
    PangoLayout *editable_layout;
    PangoLayout *additional_layout;
    gboolean have_editable, have_additional;

    /* check to see if the cached values are still valid; if so, there's
     * no work necessary
     */

    if (item->text_width >= 0 && item->text_height >= 0)
    {
        return;
    }

    have_editable = item->editable_text != NULL && item->editable_text[0] != '\0';
    have_additional = item->additional_text != NULL && item->additional_text[0] != '\0';

    /* No font or no text, then do no work. */
    if (!have_editable && !have_additional)
    {
        item->text_height = 0;
        item->text_height_for_layout = 0;
        item->text_height_for_entire_text = 0;
        item->text_width = 0;
        return;
    }

#ifdef PERFORMANCE_TEST_MEASURE_DISABLE
    /* fake out the width */
    item->text_width = 80;
    item->text_height = 20;
    item->text_height_for_layout = 20;
    item->text_height_for_entire_text = 20;
    return;
#endif

    editable_width = 0;
    editable_height = 0;
    editable_height_for_layout = 0;
    editable_height_for_entire_text = 0;
    editable_dx = 0;
    additional_width = 0;
    additional_height = 0;
    additional_dx = 0;

    container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);
    editable_layout = NULL;
    additional_layout = NULL;

    if (have_editable)
    {
        /* first, measure required text height: editable_height_for_entire_text
         * then, measure text height applicable for layout: editable_height_for_layout
         * next, measure actually displayed height: editable_height
         */
        editable_layout = get_label_layout (&item->editable_text_layout, item, item->editable_text);

        prepare_pango_layout_for_measure_entire_text (item, editable_layout);
        layout_get_full_size (editable_layout,
                              NULL,
                              &editable_height_for_entire_text,
                              NULL);
        layout_get_size_for_layout (editable_layout,
                                    nautilus_canvas_container_get_max_layout_lines (container),
                                    editable_height_for_entire_text,
                                    &editable_height_for_layout);

        prepare_pango_layout_for_draw (item, editable_layout);
        layout_get_full_size (editable_layout,
                              &editable_width,
                              &editable_height,
                              &editable_dx);
    }

    if (have_additional)
    {
        additional_layout = get_label_layout (&item->additional_text_layout, item, item->additional_text);
        prepare_pango_layout_for_draw (item, additional_layout);
        layout_get_full_size (additional_layout,
                              &additional_width, &additional_height, &additional_dx);
    }

    item->editable_text_height = editable_height;

    if (editable_width > additional_width)
    {
        item->text_width = editable_width;
        item->text_dx = editable_dx;
    }
    else
    {
        item->text_width = additional_width;
        item->text_dx = additional_dx;
    }

    if (have_additional)
    {
        item->text_height = editable_height + LABEL_LINE_SPACING + additional_height;
        item->text_height_for_layout = editable_height_for_layout + LABEL_LINE_SPACING + additional_height;
        item->text_height_for_entire_text = editable_height_for_entire_text + LABEL_LINE_SPACING + additional_height;
    }
    else
    {
        item->text_height = editable_height;
        item->text_height_for_layout = editable_height_for_layout;
        item->text_height_for_entire_text = editable_height_for_entire_text;
    }

    /* add some extra space for highlighting even when we don't highlight so things won't move */

    /* extra slop for nicer highlighting */
    item->text_height += TEXT_BACK_PADDING_Y * 2;
    item->text_height_for_layout += TEXT_BACK_PADDING_Y * 2;
    item->text_height_for_entire_text += TEXT_BACK_PADDING_Y * 2;
    item->editable_text_height += TEXT_BACK_PADDING_Y * 2;

    /* extra to make it look nicer */
    item->text_width += TEXT_BACK_PADDING_X * 2;

    if (editable_layout)
    {
        g_object_unref (editable_layout);
    }

    if (additional_layout)
    {
        g_object_unref (additional_layout);
    }
}

static void
snapshot_label (NautilusCanvasItem *item,
                GtkSnapshot        *snapshot,
                EelIRect            icon_rect)
{
    NautilusCanvasContainer *container;
    PangoLayout *editable_layout;
    PangoLayout *additional_layout;
    GtkStyleContext *context;
    GtkStateFlags state, base_state;
    gboolean have_editable, have_additional;
    gboolean needs_highlight;
    EelIRect text_rect;
    int x;
    int max_text_width;
    gdouble frame_w, frame_h, frame_x, frame_y;
    gboolean draw_frame = TRUE;

#ifdef PERFORMANCE_TEST_DRAW_DISABLE
    return;
#endif

    measure_label_text (item);
    if (item->text_height == 0 ||
        item->text_width == 0)
    {
        return;
    }

    container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);
    context = gtk_widget_get_style_context (GTK_WIDGET (container));

    text_rect = compute_text_rectangle (item, icon_rect, TRUE, BOUNDS_USAGE_FOR_DISPLAY);

    needs_highlight = item->is_highlighted_for_selection || item->is_highlighted_for_drop;

    editable_layout = NULL;
    additional_layout = NULL;

    have_editable = item->editable_text != NULL && item->editable_text[0] != '\0';
    have_additional = item->additional_text != NULL && item->additional_text[0] != '\0';
    g_assert (have_editable || have_additional);

    max_text_width = floor (nautilus_canvas_item_get_max_text_width (item));

    base_state = gtk_widget_get_state_flags (GTK_WIDGET (container));
    base_state &= ~(GTK_STATE_FLAG_SELECTED |
                    GTK_STATE_FLAG_PRELIGHT);
    state = base_state;

    /* if the canvas is highlighted, do some set-up */
    if (needs_highlight)
    {
        state |= GTK_STATE_FLAG_SELECTED;

        frame_x = text_rect.x0;
        frame_y = text_rect.y0;
        frame_w = text_rect.x1 - text_rect.x0;
        frame_h = text_rect.y1 - text_rect.y0;
    }
    else
    {
        draw_frame = FALSE;
    }

    if (draw_frame)
    {
        gtk_style_context_save (context);
        gtk_style_context_set_state (context, state);

        gtk_snapshot_render_frame (snapshot, context,
                                   frame_x, frame_y,
                                   frame_w, frame_h);
        gtk_snapshot_render_background (snapshot, context,
                                        frame_x, frame_y,
                                        frame_w, frame_h);

        gtk_style_context_restore (context);
    }

    x = text_rect.x0 + ((text_rect.x1 - text_rect.x0) - max_text_width) / 2;

    if (have_editable)
    {
        state = base_state;

        if (needs_highlight)
        {
            state |= GTK_STATE_FLAG_SELECTED;
        }

        editable_layout = get_label_layout (&item->editable_text_layout, item, item->editable_text);
        prepare_pango_layout_for_draw (item, editable_layout);

        gtk_style_context_save (context);
        gtk_style_context_set_state (context, state);

        gtk_snapshot_render_layout (snapshot, context,
                                    x, text_rect.y0 + TEXT_BACK_PADDING_Y,
                                    editable_layout);

        gtk_style_context_restore (context);
    }

    if (have_additional)
    {
        state = base_state;

        if (needs_highlight)
        {
            state |= GTK_STATE_FLAG_SELECTED;
        }

        additional_layout = get_label_layout (&item->additional_text_layout, item, item->additional_text);
        prepare_pango_layout_for_draw (item, additional_layout);

        gtk_style_context_save (context);
        gtk_style_context_set_state (context, state);
        gtk_style_context_add_class (context, "dim-label");

        gtk_snapshot_render_layout (snapshot, context,
                                    x, text_rect.y0 + item->editable_text_height + LABEL_LINE_SPACING + TEXT_BACK_PADDING_Y,
                                    additional_layout);

        gtk_style_context_restore (context);
    }

    if (item->is_highlighted_as_keyboard_focus)
    {
        if (needs_highlight)
        {
            state = GTK_STATE_FLAG_SELECTED;
        }

        gtk_style_context_save (context);
        gtk_style_context_set_state (context, state);

        gtk_snapshot_render_focus (snapshot, context,
                                   text_rect.x0,
                                   text_rect.y0,
                                   text_rect.x1 - text_rect.x0,
                                   text_rect.y1 - text_rect.y0);

        gtk_style_context_restore (context);
    }

    if (editable_layout != NULL)
    {
        g_object_unref (editable_layout);
    }

    if (additional_layout != NULL)
    {
        g_object_unref (additional_layout);
    }
}

void
nautilus_canvas_item_set_is_visible (NautilusCanvasItem *item,
                                     gboolean            visible)
{
    if (item->is_visible == visible)
    {
        return;
    }

    item->is_visible = visible;

    if (!visible)
    {
        nautilus_canvas_item_invalidate_label (item);
    }
}

void
nautilus_canvas_item_invalidate_label (NautilusCanvasItem *item)
{
    nautilus_canvas_item_invalidate_label_size (item);

    if (item->editable_text_layout)
    {
        g_object_unref (item->editable_text_layout);
        item->editable_text_layout = NULL;
    }

    if (item->additional_text_layout)
    {
        g_object_unref (item->additional_text_layout);
        item->additional_text_layout = NULL;
    }
}

static void
snapshot_icon (NautilusCanvasItem *self,
               GtkSnapshot        *snapshot,
               EelIRect            icon_rect)
{
    int width;
    int height;
    graphene_rect_t bounds;
    EelCanvasItem *item;
    GtkWidget *widget;
    GtkStyleContext *context;
    cairo_t *cr;
    cairo_surface_t *surface;
    unsigned char *data;
    int stride;

    get_scaled_icon_size (self, &width, &height);

    bounds = GRAPHENE_RECT_INIT (icon_rect.x0, icon_rect.y0, width, height);
    item = EEL_CANVAS_ITEM (self);
    widget = GTK_WIDGET (item->canvas);
    context = gtk_widget_get_style_context (widget);
    cr = gtk_snapshot_append_cairo (snapshot, &bounds);
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
    data = cairo_image_surface_get_data (surface);
    stride = cairo_image_surface_get_stride (surface);

    gdk_texture_download (self->texture, data, stride);

    cairo_surface_mark_dirty (surface);

    cairo_translate (cr, bounds.origin.x, bounds.origin.y);
    cairo_scale (cr,
                 width / gdk_texture_get_width (self->texture),
                 height / gdk_texture_get_height (self->texture));
    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_paint (cr);

    if (self->is_highlighted_for_selection || self->is_highlighted_for_drop)
    {
        cairo_set_operator (cr, CAIRO_OPERATOR_MULTIPLY);

        cairo_push_group (cr);

        gtk_style_context_save (context);
        gtk_style_context_set_state (context, GTK_STATE_FLAG_SELECTED);
        gtk_render_background (context, cr, 0, 0, width, height);
        gtk_style_context_restore (context);

        cairo_pop_group_to_source (cr);

        cairo_mask_surface (cr, surface, 0, 0);
    }

    if (self->is_prelit || self->is_highlighted_for_clipboard)
    {
        cairo_set_operator (cr, CAIRO_OPERATOR_ADD);

        cairo_push_group (cr);

        /* This is *close enough* to the original look.
         * The magic alpha value was selected after visual comparison.
         */
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.18);
        cairo_paint (cr);

        cairo_pop_group_to_source (cr);

        cairo_mask_surface (cr, surface, 0, 0);
    }

    cairo_destroy (cr);
    cairo_surface_destroy (surface);
}

GdkPaintable *
nautilus_canvas_item_get_drag_paintable (NautilusCanvasItem *item)
{
    GtkSnapshot *snapshot;
    EelCanvas *canvas;
    int pix_width, pix_height;
    int item_offset_x, item_offset_y;
    EelIRect icon_rect;
    double item_x, item_y;
    GtkStyleContext *context;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_ITEM (item), NULL);

    canvas = EEL_CANVAS_ITEM (item)->canvas;
    context = gtk_widget_get_style_context (GTK_WIDGET (canvas));

    gtk_style_context_save (context);
    gtk_style_context_add_class (context, "nautilus-canvas-item");

    /* Assume we're updated so canvas item data is right */

    /* Calculate the offset from the top-left corner of the
     *  new image to the item position (where the pixmap is placed) */
    eel_canvas_world_to_window (canvas,
                                item->x, item->y,
                                &item_x, &item_y);

    item_offset_x = item_x - EEL_CANVAS_ITEM (item)->x1;
    item_offset_y = item_y - EEL_CANVAS_ITEM (item)->y1;

    snapshot = gtk_snapshot_new ();

    gtk_snapshot_offset (snapshot, item_offset_x, item_offset_y);

    get_scaled_icon_size (item, &pix_width, &pix_height);

    icon_rect.x0 = item_offset_x;
    icon_rect.y0 = item_offset_y;
    icon_rect.x1 = icon_rect.x0 + pix_width;
    icon_rect.y1 = icon_rect.y0 + pix_height;

    snapshot_icon (item, snapshot, icon_rect);
    snapshot_label (item, snapshot, icon_rect);

    gtk_style_context_restore (context);

    return gtk_snapshot_free_to_paintable (snapshot, NULL);
}

static void
nautilus_canvas_item_snapshot (EelCanvasItem *item,
                               GtkSnapshot   *snapshot)
{
    NautilusCanvasContainer *container;
    NautilusCanvasItem *self;
    GtkStyleContext *context;

    container = NAUTILUS_CANVAS_CONTAINER (item->canvas);
    self = NAUTILUS_CANVAS_ITEM (item);

    if (self->texture == NULL)
    {
        return;
    }

    context = gtk_widget_get_style_context (GTK_WIDGET (container));
    gtk_style_context_save (context);
    gtk_style_context_add_class (context, "nautilus-canvas-item");

    snapshot_icon (self, snapshot, self->icon_rect);
    snapshot_label (self, snapshot, self->icon_rect);

    gtk_style_context_restore (context);
}

#define ZERO_WIDTH_SPACE "\xE2\x80\x8B"

static PangoLayout *
create_label_layout (NautilusCanvasItem *self,
                     const char         *text)
{
    PangoLayout *layout;
    PangoContext *context;
    PangoFontDescription *desc;
    NautilusCanvasContainer *container;
    EelCanvasItem *canvas_item;
    GString *str;
    char *zeroified_text;
    const char *p;

    canvas_item = EEL_CANVAS_ITEM (self);

    container = NAUTILUS_CANVAS_CONTAINER (canvas_item->canvas);
    context = gtk_widget_get_pango_context (GTK_WIDGET (canvas_item->canvas));
    layout = pango_layout_new (context);

    zeroified_text = NULL;

    if (text != NULL)
    {
        str = g_string_new (NULL);

        for (p = text; *p != '\0'; p++)
        {
            str = g_string_append_c (str, *p);

            if (*p == '_' || *p == '-' || (*p == '.' && !g_ascii_isdigit (*(p + 1))))
            {
                /* Ensure that we allow to break after '_' or '.' characters,
                 * if they are not followed by a number */
                str = g_string_append (str, ZERO_WIDTH_SPACE);
            }
        }

        zeroified_text = g_string_free (str, FALSE);
    }

    pango_layout_set_text (layout, zeroified_text, -1);
    pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);

    pango_layout_set_spacing (layout, LABEL_LINE_SPACING);
    pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);

    /* Create a font description */
    if (container->details->font)
    {
        desc = pango_font_description_from_string (container->details->font);
    }
    else
    {
        desc = pango_font_description_copy (pango_context_get_font_description (context));
    }
    pango_layout_set_font_description (layout, desc);
    pango_font_description_free (desc);
    g_free (zeroified_text);

    return layout;
}

static PangoLayout *
get_label_layout (PangoLayout        **layout_cache,
                  NautilusCanvasItem  *item,
                  const char          *text)
{
    PangoLayout *layout;

    if (*layout_cache != NULL)
    {
        return g_object_ref (*layout_cache);
    }

    layout = create_label_layout (item, text);

    if (item->is_visible)
    {
        *layout_cache = g_object_ref (layout);
    }

    return layout;
}

/* handle events */

static gboolean
nautilus_canvas_item_enter_notify_event (EelCanvasItem *item,
                                         EelEvent      *event)
{
    NautilusCanvasItem *canvas_item;

    canvas_item = NAUTILUS_CANVAS_ITEM (item);

    if (!canvas_item->is_prelit)
    {
        canvas_item->is_prelit = TRUE;
        nautilus_canvas_item_invalidate_label_size (canvas_item);
        eel_canvas_item_request_update (item);
        eel_canvas_item_send_behind (item,
                                     NAUTILUS_CANVAS_CONTAINER (item->canvas)->details->rubberband_info.selection_rectangle);

        /* show a hand cursor */
        if (in_single_click_mode ())
        {
            gtk_widget_set_cursor_from_name (GTK_WIDGET (item->canvas), "pointer");
        }
    }

    return GDK_EVENT_STOP;
}

static gboolean
nautilus_canvas_item_leave_notify_event (EelCanvasItem *item,
                                         EelEvent      *event)
{
    NautilusCanvasItem *canvas_item;

    canvas_item = NAUTILUS_CANVAS_ITEM (item);

    if (canvas_item->is_prelit
        || canvas_item->is_highlighted_for_drop)
    {
        /* When leaving, turn of the prelight state and the
         * higlighted for drop. The latter gets turned on
         * by the drag&drop motion callback.
         */
        canvas_item->is_prelit = FALSE;
        canvas_item->is_highlighted_for_drop = FALSE;
        nautilus_canvas_item_invalidate_label_size (canvas_item);
        eel_canvas_item_request_update (item);

        /* show default cursor */
        gtk_widget_set_cursor (GTK_WIDGET (item->canvas), NULL);
    }

    return GDK_EVENT_STOP;
}

static gboolean
nautilus_canvas_item_event (EelCanvasItem *item,
                            EelEvent      *event)
{
    GdkEventType event_type;

    event_type = eel_event_get_event_type (event);
    if (event_type == GDK_ENTER_NOTIFY)
    {
        return nautilus_canvas_item_enter_notify_event (item, event);
    }
    if (event_type == GDK_LEAVE_NOTIFY)
    {
        return nautilus_canvas_item_leave_notify_event (item, event);
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean
hit_test (NautilusCanvasItem *canvas_item,
          EelIRect            icon_rect)
{
    /* Quick check to see if the rect hits the canvas or text at all. */
    if (!eel_irect_hits_irect (canvas_item->icon_rect, icon_rect)
        && (!eel_irect_hits_irect (canvas_item->text_rect, icon_rect)))
    {
        return FALSE;
    }

    /* Check for hit in the canvas. */
    if (eel_irect_hits_irect (canvas_item->icon_rect, icon_rect))
    {
        return TRUE;
    }

    /* Check for hit in the text. */
    if (eel_irect_hits_irect (canvas_item->text_rect, icon_rect))
    {
        return TRUE;
    }

    return FALSE;
}

/* Point handler for the canvas canvas item. */
static double
nautilus_canvas_item_point (EelCanvasItem  *item,
                            double          x,
                            double          y,
                            int             cx,
                            int             cy,
                            EelCanvasItem **actual_item)
{
    EelIRect icon_rect;

    *actual_item = item;
    icon_rect.x0 = cx;
    icon_rect.y0 = cy;
    icon_rect.x1 = cx + 1;
    icon_rect.y1 = cy + 1;
    if (hit_test (NAUTILUS_CANVAS_ITEM (item), icon_rect))
    {
        return 0.0;
    }
    else
    {
        /* This value means not hit.
         * It's kind of arbitrary. Can we do better?
         */
        return item->canvas->pixels_per_unit * 2 + 10;
    }
}

static void
nautilus_canvas_item_translate (EelCanvasItem *item,
                                double         dx,
                                double         dy)
{
    NautilusCanvasItem *self;

    self = NAUTILUS_CANVAS_ITEM (item);

    self->x += dx;
    self->y += dy;
}

void
nautilus_canvas_item_get_bounds_for_layout (NautilusCanvasItem *self,
                                            double             *x1,
                                            double             *y1,
                                            double             *x2,
                                            double             *y2)
{
    EelIRect *total_rect;

    g_return_if_fail (NAUTILUS_IS_CANVAS_ITEM (self));

    nautilus_canvas_item_ensure_bounds_up_to_date (self);
    g_assert (self->bounds_cached);

    total_rect = &self->bounds_cache_for_layout;

    /* Return the result. */
    if (x1 != NULL)
    {
        *x1 = (int) self->x + total_rect->x0;
    }
    if (y1 != NULL)
    {
        *y1 = (int) self->y + total_rect->y0;
    }
    if (x2 != NULL)
    {
        *x2 = (int) self->x + total_rect->x1 + 1;
    }
    if (y2 != NULL)
    {
        *y2 = (int) self->y + total_rect->y1 + 1;
    }
}

void
nautilus_canvas_item_get_bounds_for_entire_item (NautilusCanvasItem *self,
                                                 double             *x1,
                                                 double             *y1,
                                                 double             *x2,
                                                 double             *y2)
{
    EelIRect *total_rect;

    g_return_if_fail (NAUTILUS_IS_CANVAS_ITEM (self));

    nautilus_canvas_item_ensure_bounds_up_to_date (self);
    g_assert (self->bounds_cached);

    total_rect = &self->bounds_cache_for_entire_item;

    /* Return the result. */
    if (x1 != NULL)
    {
        *x1 = (int) self->x + total_rect->x0;
    }
    if (y1 != NULL)
    {
        *y1 = (int) self->y + total_rect->y0;
    }
    if (x2 != NULL)
    {
        *x2 = (int) self->x + total_rect->x1 + 1;
    }
    if (y2 != NULL)
    {
        *y2 = (int) self->y + total_rect->y1 + 1;
    }
}

/* Bounds handler for the canvas canvas item. */
static void
nautilus_canvas_item_bounds (EelCanvasItem *item,
                             double        *x1,
                             double        *y1,
                             double        *x2,
                             double        *y2)
{
    NautilusCanvasItem *self;
    EelIRect *total_rect;

    self = NAUTILUS_CANVAS_ITEM (item);

    g_assert (x1 != NULL);
    g_assert (y1 != NULL);
    g_assert (x2 != NULL);
    g_assert (y2 != NULL);

    nautilus_canvas_item_ensure_bounds_up_to_date (self);
    g_assert (self->bounds_cached);

    total_rect = &self->bounds_cache;

    /* Return the result. */
    *x1 = (int) self->x + total_rect->x0;
    *y1 = (int) self->y + total_rect->y0;
    *x2 = (int) self->x + total_rect->x1 + 1;
    *y2 = (int) self->y + total_rect->y1 + 1;
}

static void
nautilus_canvas_item_ensure_bounds_up_to_date (NautilusCanvasItem *self)
{
    EelIRect icon_rect;
    EelIRect text_rect, text_rect_for_layout, text_rect_for_entire_text;
    EelIRect total_rect, total_rect_for_layout, total_rect_for_entire_text;
    EelCanvasItem *item;
    double pixels_per_unit;
    gint width, height;

    item = EEL_CANVAS_ITEM (self);

    if (!self->bounds_cached)
    {
        measure_label_text (self);

        pixels_per_unit = EEL_CANVAS_ITEM (item)->canvas->pixels_per_unit;

        /* Compute scaled canvas rectangle. */
        icon_rect.x0 = 0;
        icon_rect.y0 = 0;

        get_scaled_icon_size (self, &width, &height);

        icon_rect.x1 = width / pixels_per_unit;
        icon_rect.y1 = height / pixels_per_unit;

        /* Compute text rectangle. */
        text_rect = compute_text_rectangle (self, icon_rect, FALSE, BOUNDS_USAGE_FOR_DISPLAY);
        text_rect_for_layout = compute_text_rectangle (self, icon_rect, FALSE, BOUNDS_USAGE_FOR_LAYOUT);
        text_rect_for_entire_text = compute_text_rectangle (self, icon_rect, FALSE, BOUNDS_USAGE_FOR_ENTIRE_ITEM);

        /* Compute total rectangle */
        eel_irect_union (&total_rect, &icon_rect, &text_rect);
        eel_irect_union (&total_rect_for_layout, &icon_rect, &text_rect_for_layout);
        eel_irect_union (&total_rect_for_entire_text, &icon_rect, &text_rect_for_entire_text);

        self->bounds_cache = total_rect;
        self->bounds_cache_for_layout = total_rect_for_layout;
        self->bounds_cache_for_entire_item = total_rect_for_entire_text;
        self->bounds_cached = TRUE;
    }
}

/* Get the rectangle of the canvas only, in world coordinates. */
EelDRect
nautilus_canvas_item_get_icon_rectangle (NautilusCanvasItem *item)
{
    EelDRect rectangle;
    double pixels_per_unit;
    gint width, height;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_ITEM (item), eel_drect_empty);

    rectangle.x0 = item->x;
    rectangle.y0 = item->y;

    pixels_per_unit = EEL_CANVAS_ITEM (item)->canvas->pixels_per_unit;
    get_scaled_icon_size (NAUTILUS_CANVAS_ITEM (item), &width, &height);
    rectangle.x1 = rectangle.x0 + width / pixels_per_unit;
    rectangle.y1 = rectangle.y0 + height / pixels_per_unit;

    return rectangle;
}

/* Get the rectangle of the icon only, in canvas coordinates. */
static void
get_icon_rectangle (NautilusCanvasItem *item,
                    EelIRect           *rect)
{
    gint width, height;

    g_assert (NAUTILUS_IS_CANVAS_ITEM (item));
    g_assert (rect != NULL);


    eel_canvas_w2c (EEL_CANVAS_ITEM (item)->canvas,
                    item->x,
                    item->y,
                    &rect->x0,
                    &rect->y0);

    get_scaled_icon_size (item, &width, &height);

    rect->x1 = rect->x0 + width;
    rect->y1 = rect->y0 + height;
}

/* nautilus_canvas_item_hit_test_rectangle
 *
 * Check and see if there is an intersection between the item and the
 * canvas rect.
 */
gboolean
nautilus_canvas_item_hit_test_rectangle (NautilusCanvasItem *item,
                                         EelIRect            icon_rect)
{
    g_return_val_if_fail (NAUTILUS_IS_CANVAS_ITEM (item), FALSE);

    return hit_test (item, icon_rect);
}

void
nautilus_canvas_item_set_entire_text (NautilusCanvasItem *item,
                                      gboolean            entire_text)
{
    if (item->entire_text != entire_text)
    {
        item->entire_text = entire_text;

        nautilus_canvas_item_invalidate_label_size (item);
        eel_canvas_item_request_update (EEL_CANVAS_ITEM (item));
    }
}

NautilusCanvasIcon *
nautilus_canvas_item_get_icon (NautilusCanvasItem *self)
{
    g_return_val_if_fail (NAUTILUS_IS_CANVAS_ITEM (self), NULL);

    return self->icon;
}

void
nautilus_canvas_item_set_icon (NautilusCanvasItem *self,
                               NautilusCanvasIcon *icon)
{
    g_return_if_fail (NAUTILUS_IS_CANVAS_ITEM (self));

    self->icon = icon;
}

/* Class initialization function for the canvas canvas item. */
static void
nautilus_canvas_item_class_init (NautilusCanvasItemClass *class)
{
    GObjectClass *object_class;
    EelCanvasItemClass *item_class;

    object_class = G_OBJECT_CLASS (class);
    item_class = EEL_CANVAS_ITEM_CLASS (class);

    object_class->dispose = nautilus_canvas_item_dispose;
    object_class->finalize = nautilus_canvas_item_finalize;
    object_class->set_property = nautilus_canvas_item_set_property;
    object_class->get_property = nautilus_canvas_item_get_property;

    g_object_class_install_property (
        object_class,
        PROP_EDITABLE_TEXT,
        g_param_spec_string ("editable_text",
                             "editable text",
                             "the editable label",
                             "", G_PARAM_READWRITE));

    g_object_class_install_property (
        object_class,
        PROP_ADDITIONAL_TEXT,
        g_param_spec_string ("additional_text",
                             "additional text",
                             "some more text",
                             "", G_PARAM_READWRITE));

    g_object_class_install_property (
        object_class,
        PROP_HIGHLIGHTED_FOR_SELECTION,
        g_param_spec_boolean ("highlighted_for_selection",
                              "highlighted for selection",
                              "whether we are highlighted for a selection",
                              FALSE, G_PARAM_READWRITE));

    g_object_class_install_property (
        object_class,
        PROP_HIGHLIGHTED_AS_KEYBOARD_FOCUS,
        g_param_spec_boolean ("highlighted_as_keyboard_focus",
                              "highlighted as keyboard focus",
                              "whether we are highlighted to render keyboard focus",
                              FALSE, G_PARAM_READWRITE));


    g_object_class_install_property (
        object_class,
        PROP_HIGHLIGHTED_FOR_DROP,
        g_param_spec_boolean ("highlighted_for_drop",
                              "highlighted for drop",
                              "whether we are highlighted for a D&D drop",
                              FALSE, G_PARAM_READWRITE));

    g_object_class_install_property (
        object_class,
        PROP_HIGHLIGHTED_FOR_CLIPBOARD,
        g_param_spec_boolean ("highlighted_for_clipboard",
                              "highlighted for clipboard",
                              "whether we are highlighted for a clipboard paste (after we have been cut)",
                              FALSE, G_PARAM_READWRITE));

    item_class->update = nautilus_canvas_item_update;
    item_class->snapshot = nautilus_canvas_item_snapshot;
    item_class->point = nautilus_canvas_item_point;
    item_class->translate = nautilus_canvas_item_translate;
    item_class->bounds = nautilus_canvas_item_bounds;
    item_class->event = nautilus_canvas_item_event;

    atk_registry_set_factory_type (atk_get_default_registry (),
                                   NAUTILUS_TYPE_CANVAS_ITEM,
                                   nautilus_canvas_item_accessible_factory_get_type ());
}

/* ============================= a11y interfaces =========================== */

static const char *nautilus_canvas_item_accessible_action_names[] =
{
    "open",
    "menu",
    NULL
};

static const char *nautilus_canvas_item_accessible_action_descriptions[] =
{
    "Open item",
    "Popup context menu",
    NULL
};

enum
{
    ACTION_OPEN,
    ACTION_MENU,
    LAST_ACTION
};

typedef struct
{
    char *action_descriptions[LAST_ACTION];
    char *image_description;
    char *description;
} NautilusCanvasItemAccessiblePrivate;

typedef struct
{
    NautilusCanvasItem *item;
    gint action_number;
} NautilusCanvasItemAccessibleActionContext;

typedef struct
{
    EelCanvasItemAccessible parent;
    NautilusCanvasItemAccessiblePrivate *priv;
} NautilusCanvasItemAccessible;

typedef struct
{
    EelCanvasItemAccessibleClass parent_class;
} NautilusCanvasItemAccessibleClass;

#define GET_ACCESSIBLE_PRIV(o) ((NautilusCanvasItemAccessible *) o)->priv;

/* accessible AtkAction interface */
static gboolean
nautilus_canvas_item_accessible_idle_do_action (gpointer data)
{
    NautilusCanvasItem *item;
    NautilusCanvasItemAccessibleActionContext *ctx;
    NautilusCanvasIcon *icon;
    NautilusCanvasContainer *container;
    GList file_list;
    gint action_number;

    container = NAUTILUS_CANVAS_CONTAINER (data);
    container->details->a11y_item_action_idle_handler = 0;
    while (!g_queue_is_empty (container->details->a11y_item_action_queue))
    {
        ctx = g_queue_pop_head (container->details->a11y_item_action_queue);
        action_number = ctx->action_number;
        item = ctx->item;
        g_free (ctx);
        icon = item->icon;

        switch (action_number)
        {
            case ACTION_OPEN:
            {
                file_list.data = icon->file;
                file_list.next = NULL;
                file_list.prev = NULL;
                g_signal_emit_by_name (container, "activate", &file_list);
            }
            break;

            case ACTION_MENU:
            {
                g_autoptr (GList) selection = NULL;
                g_autoptr (GdkEvent) event = NULL;

                selection = nautilus_canvas_container_get_selection (container);
                if (selection == NULL ||
                    g_list_length (selection) != 1 ||
                    selection->data != icon->file)
                {
                    return FALSE;
                }
                event = gdk_event_new (GDK_NOTHING);

                g_signal_emit_by_name (container, "context-click-selection", event);
            }
            break;

            default:
            {
                g_assert_not_reached ();
            }
            break;
        }
    }
    return FALSE;
}

static gboolean
nautilus_canvas_item_accessible_do_action (AtkAction *accessible,
                                           int        i)
{
    NautilusCanvasItem *item;
    NautilusCanvasItemAccessibleActionContext *ctx;
    NautilusCanvasContainer *container;

    g_assert (i < LAST_ACTION);

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (accessible)));
    if (!item)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);
    switch (i)
    {
        case ACTION_OPEN:
        case ACTION_MENU:
        {
            if (container->details->a11y_item_action_queue == NULL)
            {
                container->details->a11y_item_action_queue = g_queue_new ();
            }
            ctx = g_new (NautilusCanvasItemAccessibleActionContext, 1);
            ctx->action_number = i;
            ctx->item = item;
            g_queue_push_head (container->details->a11y_item_action_queue, ctx);
            if (container->details->a11y_item_action_idle_handler == 0)
            {
                container->details->a11y_item_action_idle_handler = g_idle_add (nautilus_canvas_item_accessible_idle_do_action, container);
            }
        }
        break;

        default:
            g_warning ("Invalid action passed to NautilusCanvasItemAccessible::do_action");
            return FALSE;
    }

    return TRUE;
}

static int
nautilus_canvas_item_accessible_get_n_actions (AtkAction *accessible)
{
    return LAST_ACTION;
}

static const char *
nautilus_canvas_item_accessible_action_get_description (AtkAction *accessible,
                                                        int        i)
{
    NautilusCanvasItemAccessiblePrivate *priv;

    g_assert (i < LAST_ACTION);

    priv = GET_ACCESSIBLE_PRIV (accessible);

    if (priv->action_descriptions[i])
    {
        return priv->action_descriptions[i];
    }
    else
    {
        return nautilus_canvas_item_accessible_action_descriptions[i];
    }
}

static const char *
nautilus_canvas_item_accessible_action_get_name (AtkAction *accessible,
                                                 int        i)
{
    g_assert (i < LAST_ACTION);

    return nautilus_canvas_item_accessible_action_names[i];
}

static const char *
nautilus_canvas_item_accessible_action_get_keybinding (AtkAction *accessible,
                                                       int        i)
{
    g_assert (i < LAST_ACTION);

    return NULL;
}

static gboolean
nautilus_canvas_item_accessible_action_set_description (AtkAction  *accessible,
                                                        int         i,
                                                        const char *description)
{
    NautilusCanvasItemAccessiblePrivate *priv;

    g_assert (i < LAST_ACTION);

    priv = GET_ACCESSIBLE_PRIV (accessible);

    if (priv->action_descriptions[i])
    {
        g_free (priv->action_descriptions[i]);
    }
    priv->action_descriptions[i] = g_strdup (description);

    return TRUE;
}

static void
nautilus_canvas_item_accessible_action_interface_init (AtkActionIface *iface)
{
    iface->do_action = nautilus_canvas_item_accessible_do_action;
    iface->get_n_actions = nautilus_canvas_item_accessible_get_n_actions;
    iface->get_description = nautilus_canvas_item_accessible_action_get_description;
    iface->get_keybinding = nautilus_canvas_item_accessible_action_get_keybinding;
    iface->get_name = nautilus_canvas_item_accessible_action_get_name;
    iface->set_description = nautilus_canvas_item_accessible_action_set_description;
}

static const gchar *
nautilus_canvas_item_accessible_get_name (AtkObject *accessible)
{
    NautilusCanvasItem *item;

    if (accessible->name)
    {
        return accessible->name;
    }

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (accessible)));
    if (!item)
    {
        return NULL;
    }
    return item->editable_text;
}

static const gchar *
nautilus_canvas_item_accessible_get_description (AtkObject *accessible)
{
    NautilusCanvasItem *item;

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (accessible)));
    if (!item)
    {
        return NULL;
    }

    return item->additional_text;
}

static AtkObject *
nautilus_canvas_item_accessible_get_parent (AtkObject *accessible)
{
    NautilusCanvasItem *item;

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (accessible)));
    if (!item)
    {
        return NULL;
    }

    return gtk_widget_get_accessible (GTK_WIDGET (EEL_CANVAS_ITEM (item)->canvas));
}

static int
nautilus_canvas_item_accessible_get_index_in_parent (AtkObject *accessible)
{
    NautilusCanvasItem *item;
    NautilusCanvasContainer *container;
    GList *l;
    NautilusCanvasIcon *icon;
    int i;

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (accessible)));
    if (!item)
    {
        return -1;
    }

    container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);

    l = container->details->icons;
    i = 0;
    while (l)
    {
        icon = l->data;

        if (icon->item == item)
        {
            return i;
        }

        i++;
        l = l->next;
    }

    return -1;
}

static const gchar *
nautilus_canvas_item_accessible_get_image_description (AtkImage *image)
{
    NautilusCanvasItemAccessiblePrivate *priv;
    NautilusCanvasItem *item;
    NautilusCanvasIcon *icon;
    NautilusCanvasContainer *container;
    char *description;

    priv = GET_ACCESSIBLE_PRIV (image);

    if (priv->image_description)
    {
        return priv->image_description;
    }
    else
    {
        item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (image)));
        if (item == NULL)
        {
            return NULL;
        }
        icon = item->icon;
        container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);
        description = nautilus_canvas_container_get_icon_description (container, icon->file);
        g_free (priv->description);
        priv->description = description;
        return priv->description;
    }
}

static void
nautilus_canvas_item_accessible_get_image_size (AtkImage *image,
                                                gint     *width,
                                                gint     *height)
{
    NautilusCanvasItem *item;

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (image)));
    get_scaled_icon_size (item, width, height);
}

static void
nautilus_canvas_item_accessible_get_image_position (AtkImage     *image,
                                                    gint         *x,
                                                    gint         *y,
                                                    AtkCoordType  coord_type)
{
    NautilusCanvasItem *item;
    gint x_offset, y_offset, itmp;

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (image)));
    if (!item)
    {
        return;
    }
    if (!item->icon_rect.x0 && !item->icon_rect.x1)
    {
        return;
    }
    else
    {
        x_offset = 0;
        y_offset = 0;
        if (item->text_width)
        {
            itmp = item->icon_rect.x0 -
                   item->text_rect.x0;
            if (itmp > x_offset)
            {
                x_offset = itmp;
            }
            itmp = item->icon_rect.y0 -
                   item->text_rect.y0;
            if (itmp > y_offset)
            {
                y_offset = itmp;
            }
        }
    }
    atk_component_get_extents (ATK_COMPONENT (image), x, y, NULL, NULL, coord_type);
    *x += x_offset;
    *y += y_offset;
}

static gboolean
nautilus_canvas_item_accessible_set_image_description (AtkImage    *image,
                                                       const gchar *description)
{
    NautilusCanvasItemAccessiblePrivate *priv;

    priv = GET_ACCESSIBLE_PRIV (image);

    g_free (priv->image_description);
    priv->image_description = g_strdup (description);

    return TRUE;
}

static void
nautilus_canvas_item_accessible_image_interface_init (AtkImageIface *iface)
{
    iface->get_image_description = nautilus_canvas_item_accessible_get_image_description;
    iface->set_image_description = nautilus_canvas_item_accessible_set_image_description;
    iface->get_image_size = nautilus_canvas_item_accessible_get_image_size;
    iface->get_image_position = nautilus_canvas_item_accessible_get_image_position;
}

/* accessible text interface */
static gint
nautilus_canvas_item_accessible_get_offset_at_point (AtkText      *text,
                                                     gint          x,
                                                     gint          y,
                                                     AtkCoordType  coords)
{
    gint real_x, real_y, real_width, real_height;
    NautilusCanvasItem *item;
    gint editable_height;
    gint offset = 0;
    gint index;
    PangoLayout *layout, *editable_layout, *additional_layout;
    PangoRectangle rect0;
    char *canvas_text;
    gboolean have_editable;
    gboolean have_additional;
    gint text_offset, height;

    atk_component_get_extents (ATK_COMPONENT (text), &real_x, &real_y,
                               &real_width, &real_height, coords);

    x -= real_x;
    y -= real_y;

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (text)));

    if (item->texture != NULL)
    {
        get_scaled_icon_size (item, NULL, &height);
        y -= height;
    }
    have_editable = item->editable_text != NULL &&
                    item->editable_text[0] != '\0';
    have_additional = item->additional_text != NULL && item->additional_text[0] != '\0';

    editable_layout = NULL;
    additional_layout = NULL;
    if (have_editable)
    {
        editable_layout = get_label_layout (&item->editable_text_layout, item, item->editable_text);
        prepare_pango_layout_for_draw (item, editable_layout);
        pango_layout_get_pixel_size (editable_layout, NULL, &editable_height);
        if (y >= editable_height &&
            have_additional)
        {
            prepare_pango_layout_for_draw (item, editable_layout);
            additional_layout = get_label_layout (&item->additional_text_layout, item, item->additional_text);
            layout = additional_layout;
            canvas_text = item->additional_text;
            y -= editable_height + LABEL_LINE_SPACING;
        }
        else
        {
            layout = editable_layout;
            canvas_text = item->editable_text;
        }
    }
    else if (have_additional)
    {
        additional_layout = get_label_layout (&item->additional_text_layout, item, item->additional_text);
        prepare_pango_layout_for_draw (item, additional_layout);
        layout = additional_layout;
        canvas_text = item->additional_text;
    }
    else
    {
        return 0;
    }

    text_offset = 0;
    if (have_editable)
    {
        pango_layout_index_to_pos (editable_layout, 0, &rect0);
        text_offset = PANGO_PIXELS (rect0.x);
    }
    if (have_additional)
    {
        gint itmp;

        pango_layout_index_to_pos (additional_layout, 0, &rect0);
        itmp = PANGO_PIXELS (rect0.x);
        if (itmp < text_offset)
        {
            text_offset = itmp;
        }
    }
    pango_layout_index_to_pos (layout, 0, &rect0);
    x += text_offset;
    if (!pango_layout_xy_to_index (layout,
                                   x * PANGO_SCALE,
                                   y * PANGO_SCALE,
                                   &index, NULL))
    {
        if (x < 0 || y < 0)
        {
            index = 0;
        }
        else
        {
            index = -1;
        }
    }
    if (index == -1)
    {
        offset = g_utf8_strlen (canvas_text, -1);
    }
    else
    {
        offset = g_utf8_pointer_to_offset (canvas_text, canvas_text + index);
    }
    if (layout == additional_layout)
    {
        offset += g_utf8_strlen (item->editable_text, -1);
    }

    if (editable_layout != NULL)
    {
        g_object_unref (editable_layout);
    }

    if (additional_layout != NULL)
    {
        g_object_unref (additional_layout);
    }

    return offset;
}

static void
nautilus_canvas_item_accessible_get_character_extents (AtkText      *text,
                                                       gint          offset,
                                                       gint         *x,
                                                       gint         *y,
                                                       gint         *width,
                                                       gint         *height,
                                                       AtkCoordType  coords)
{
    gint pos_x, pos_y;
    gint len, byte_offset;
    gint editable_height;
    gchar *canvas_text;
    NautilusCanvasItem *item;
    PangoLayout *layout, *editable_layout, *additional_layout;
    PangoRectangle rect;
    PangoRectangle rect0;
    gboolean have_editable;
    gint text_offset, pix_height;

    atk_component_get_extents (ATK_COMPONENT (text), &pos_x, &pos_y, NULL, NULL, coords);
    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (text)));

    if (item->texture != NULL)
    {
        get_scaled_icon_size (item, NULL, &pix_height);
        pos_y += pix_height;
    }

    have_editable = item->editable_text != NULL &&
                    item->editable_text[0] != '\0';
    if (have_editable)
    {
        len = g_utf8_strlen (item->editable_text, -1);
    }
    else
    {
        len = 0;
    }

    editable_layout = get_label_layout (&item->editable_text_layout, item, item->editable_text);
    additional_layout = get_label_layout (&item->additional_text_layout, item, item->additional_text);

    if (offset < len)
    {
        canvas_text = item->editable_text;
        layout = editable_layout;
    }
    else
    {
        offset -= len;
        canvas_text = item->additional_text;
        layout = additional_layout;
        pos_y += LABEL_LINE_SPACING;
        if (have_editable)
        {
            pango_layout_get_pixel_size (editable_layout, NULL, &editable_height);
            pos_y += editable_height;
        }
    }
    byte_offset = g_utf8_offset_to_pointer (canvas_text, offset) - canvas_text;
    pango_layout_index_to_pos (layout, byte_offset, &rect);
    text_offset = 0;
    if (have_editable)
    {
        pango_layout_index_to_pos (editable_layout, 0, &rect0);
        text_offset = PANGO_PIXELS (rect0.x);
    }
    if (item->additional_text != NULL &&
        item->additional_text[0] != '\0')
    {
        gint itmp;

        pango_layout_index_to_pos (additional_layout, 0, &rect0);
        itmp = PANGO_PIXELS (rect0.x);
        if (itmp < text_offset)
        {
            text_offset = itmp;
        }
    }

    g_object_unref (editable_layout);
    g_object_unref (additional_layout);

    *x = pos_x + PANGO_PIXELS (rect.x) - text_offset;
    *y = pos_y + PANGO_PIXELS (rect.y);
    *width = PANGO_PIXELS (rect.width);
    *height = PANGO_PIXELS (rect.height);
}

static char *
nautilus_canvas_item_accessible_text_get_text (AtkText *text,
                                               gint     start_pos,
                                               gint     end_pos)
{
    GObject *object;
    NautilusCanvasItem *item;

    object = atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (text));
    item = NAUTILUS_CANVAS_ITEM (object);

    return g_utf8_substring (item->text->str, start_pos, end_pos);
}

static gunichar
nautilus_canvas_item_accessible_text_get_character_at_offset (AtkText *text,
                                                              gint     offset)
{
    GObject *object;
    NautilusCanvasItem *item;
    gchar *pointer;

    object = atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (text));
    item = NAUTILUS_CANVAS_ITEM (object);
    pointer = g_utf8_offset_to_pointer (item->text->str, offset);

    return g_utf8_get_char (pointer);
}

static gint
nautilus_canvas_item_accessible_text_get_character_count (AtkText *text)
{
    GObject *object;
    NautilusCanvasItem *item;

    object = atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (text));
    item = NAUTILUS_CANVAS_ITEM (object);

    return g_utf8_strlen (item->text->str, -1);
}

static void
nautilus_canvas_item_accessible_text_interface_init (AtkTextIface *iface)
{
    iface->get_text = nautilus_canvas_item_accessible_text_get_text;
    iface->get_character_at_offset = nautilus_canvas_item_accessible_text_get_character_at_offset;
    iface->get_character_count = nautilus_canvas_item_accessible_text_get_character_count;
    iface->get_character_extents = nautilus_canvas_item_accessible_get_character_extents;
    iface->get_offset_at_point = nautilus_canvas_item_accessible_get_offset_at_point;
}

static GType nautilus_canvas_item_accessible_get_type (void);

G_DEFINE_TYPE_WITH_CODE (NautilusCanvasItemAccessible,
                         nautilus_canvas_item_accessible,
                         eel_canvas_item_accessible_get_type (),
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_IMAGE,
                                                nautilus_canvas_item_accessible_image_interface_init)
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_TEXT,
                                                nautilus_canvas_item_accessible_text_interface_init)
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_ACTION,
                                                nautilus_canvas_item_accessible_action_interface_init));

static AtkStateSet *
nautilus_canvas_item_accessible_ref_state_set (AtkObject *accessible)
{
    AtkStateSet *state_set;
    NautilusCanvasItem *item;
    NautilusCanvasContainer *container;
    GList *selection;
    gboolean one_item_selected;

    state_set = ATK_OBJECT_CLASS (nautilus_canvas_item_accessible_parent_class)->ref_state_set (accessible);

    item = NAUTILUS_CANVAS_ITEM (atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (accessible)));
    if (!item)
    {
        atk_state_set_add_state (state_set, ATK_STATE_DEFUNCT);
        return state_set;
    }
    container = NAUTILUS_CANVAS_CONTAINER (EEL_CANVAS_ITEM (item)->canvas);
    if (item->is_highlighted_as_keyboard_focus)
    {
        atk_state_set_add_state (state_set, ATK_STATE_FOCUSED);
    }
    else if (!container->details->keyboard_focus)
    {
        selection = nautilus_canvas_container_get_selection (container);
        one_item_selected = (g_list_length (selection) == 1) &&
                            item->is_highlighted_for_selection;

        if (one_item_selected)
        {
            atk_state_set_add_state (state_set, ATK_STATE_FOCUSED);
        }

        g_list_free (selection);
    }

    return state_set;
}

static void
nautilus_canvas_item_accessible_finalize (GObject *object)
{
    NautilusCanvasItemAccessiblePrivate *priv;
    int i;

    priv = GET_ACCESSIBLE_PRIV (object);

    for (i = 0; i < LAST_ACTION; i++)
    {
        g_free (priv->action_descriptions[i]);
    }
    g_free (priv->image_description);
    g_free (priv->description);

    G_OBJECT_CLASS (nautilus_canvas_item_accessible_parent_class)->finalize (object);
}

static void
nautilus_canvas_item_accessible_initialize (AtkObject *accessible,
                                            gpointer   widget)
{
    ATK_OBJECT_CLASS (nautilus_canvas_item_accessible_parent_class)->initialize (accessible, widget);

    atk_object_set_role (accessible, ATK_ROLE_CANVAS);
}

static void
nautilus_canvas_item_accessible_class_init (NautilusCanvasItemAccessibleClass *klass)
{
    AtkObjectClass *aclass = ATK_OBJECT_CLASS (klass);
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->finalize = nautilus_canvas_item_accessible_finalize;

    aclass->initialize = nautilus_canvas_item_accessible_initialize;

    aclass->get_name = nautilus_canvas_item_accessible_get_name;
    aclass->get_description = nautilus_canvas_item_accessible_get_description;
    aclass->get_parent = nautilus_canvas_item_accessible_get_parent;
    aclass->get_index_in_parent = nautilus_canvas_item_accessible_get_index_in_parent;
    aclass->ref_state_set = nautilus_canvas_item_accessible_ref_state_set;

    g_type_class_add_private (klass, sizeof (NautilusCanvasItemAccessiblePrivate));
}

static void
nautilus_canvas_item_accessible_init (NautilusCanvasItemAccessible *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, nautilus_canvas_item_accessible_get_type (),
                                              NautilusCanvasItemAccessiblePrivate);
}

/* dummy typedef */
typedef AtkObjectFactory NautilusCanvasItemAccessibleFactory;
typedef AtkObjectFactoryClass NautilusCanvasItemAccessibleFactoryClass;

G_DEFINE_TYPE (NautilusCanvasItemAccessibleFactory, nautilus_canvas_item_accessible_factory,
               ATK_TYPE_OBJECT_FACTORY);

static AtkObject *
nautilus_canvas_item_accessible_factory_create_accessible (GObject *for_object)
{
    AtkObject *accessible;
    NautilusCanvasItem *item;

    item = NAUTILUS_CANVAS_ITEM (for_object);
    g_assert (item != NULL);

    item->text = g_string_new (NULL);
    if (item->editable_text)
    {
        g_string_append (item->text, item->editable_text);
    }
    if (item->additional_text)
    {
        g_string_append (item->text, item->additional_text);
    }

    accessible = g_object_new (nautilus_canvas_item_accessible_get_type (), NULL);
    atk_object_initialize (accessible, for_object);

    return accessible;
}

static GType
nautilus_canvas_item_accessible_factory_get_accessible_type (void)
{
    return nautilus_canvas_item_accessible_get_type ();
}

static void
nautilus_canvas_item_accessible_factory_init (NautilusCanvasItemAccessibleFactory *self)
{
}

static void
nautilus_canvas_item_accessible_factory_class_init (NautilusCanvasItemAccessibleFactoryClass *klass)
{
    klass->create_accessible = nautilus_canvas_item_accessible_factory_create_accessible;
    klass->get_accessible_type = nautilus_canvas_item_accessible_factory_get_accessible_type;
}
