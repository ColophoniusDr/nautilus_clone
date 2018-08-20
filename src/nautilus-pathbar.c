/* nautilus-pathbar.c
 * Copyright (C) 2004  Red Hat, Inc.,  Jonathan Blandford <jrb@gnome.org>
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
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "nautilus-pathbar.h"
#include "nautilus-properties-window.h"

#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-global-preferences.h"
#include "nautilus-icon-names.h"
#include "nautilus-trash-monitor.h"
#include "nautilus-ui-utilities.h"

#include "nautilus-window-slot-dnd.h"

enum
{
    OPEN_LOCATION,
    PATH_CLICKED,
    LAST_SIGNAL
};

typedef enum
{
    NORMAL_BUTTON,
    OTHER_LOCATIONS_BUTTON,
    ROOT_BUTTON,
    ADMIN_ROOT_BUTTON,
    HOME_BUTTON,
    STARRED_BUTTON,
    RECENT_BUTTON,
    MOUNT_BUTTON,
} ButtonType;

#define BUTTON_DATA(x) ((ButtonData *) (x))

static guint path_bar_signals [LAST_SIGNAL] = { 0 };

#define NAUTILUS_PATH_BAR_ICON_SIZE 16
#define NAUTILUS_PATH_BAR_BUTTON_MAX_WIDTH 175

typedef struct
{
    GtkWidget *button;
    ButtonType type;
    char *dir_name;
    GFile *path;
    NautilusFile *file;
    unsigned int file_changed_signal_id;

    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *bold_label;
    GtkWidget *separator;
    GtkWidget *disclosure_arrow;
    GtkWidget *container;

    NautilusPathBar *path_bar;

    guint ignore_changes : 1;
    guint is_root : 1;
} ButtonData;

typedef struct
{
    GdkWindow *event_window;

    GFile *current_path;
    gpointer current_button_data;

    GList *button_list;
    guint settings_signal_id;

    GActionGroup *action_group;

    NautilusFile *context_menu_file;
    GtkPopover *current_view_menu_popover;
    GtkPopover *button_menu_popover;
    GMenu *current_view_menu;
    GMenu *button_menu;
    GMenu *extensions_background_menu;
    GMenu *templates_menu;
} NautilusPathBarPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (NautilusPathBar, nautilus_path_bar,
                            GTK_TYPE_CONTAINER);

static void nautilus_path_bar_check_icon_theme (NautilusPathBar *self);
static void nautilus_path_bar_update_button_appearance (ButtonData *button_data);
static void nautilus_path_bar_update_button_state (ButtonData *button_data,
                                                   gboolean    current_dir);
static void nautilus_path_bar_update_path (NautilusPathBar *self,
                                           GFile           *file_path);

static void     unschedule_pop_up_context_menu (NautilusPathBar *self);
static void     action_pathbar_open_item_new_window (GSimpleAction *action,
                                                     GVariant      *state,
                                                     gpointer       user_data);
static void     action_pathbar_open_item_new_tab (GSimpleAction *action,
                                                  GVariant      *state,
                                                  gpointer       user_data);
static void     action_pathbar_properties (GSimpleAction *action,
                                           GVariant      *state,
                                           gpointer       user_data);
static void     pop_up_pathbar_context_menu (NautilusPathBar *self,
                                             NautilusFile    *file);

const GActionEntry path_bar_actions[] =
{
    { "open-item-new-tab", action_pathbar_open_item_new_tab },
    { "open-item-new-window", action_pathbar_open_item_new_window },
    { "properties", action_pathbar_properties}
};

static void
action_pathbar_open_item_new_tab (GSimpleAction *action,
                                  GVariant      *state,
                                  gpointer       user_data)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GFile *location;

    self = NAUTILUS_PATH_BAR (user_data);
    priv = nautilus_path_bar_get_instance_private (self);

    if (!priv->context_menu_file)
    {
        return;
    }

    location = nautilus_file_get_location (priv->context_menu_file);

    if (location)
    {
        g_signal_emit (user_data, path_bar_signals[OPEN_LOCATION], 0, location, GTK_PLACES_OPEN_NEW_TAB);
        g_object_unref (location);
    }
}

static void
action_pathbar_open_item_new_window (GSimpleAction *action,
                                     GVariant      *state,
                                     gpointer       user_data)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GFile *location;

    self = NAUTILUS_PATH_BAR (user_data);
    priv = nautilus_path_bar_get_instance_private (self);

    if (!priv->context_menu_file)
    {
        return;
    }

    location = nautilus_file_get_location (priv->context_menu_file);

    if (location)
    {
        g_signal_emit (user_data, path_bar_signals[OPEN_LOCATION], 0, location, GTK_PLACES_OPEN_NEW_WINDOW);
        g_object_unref (location);
    }
}

static void
action_pathbar_properties (GSimpleAction *action,
                           GVariant      *state,
                           gpointer       user_data)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GList *files;

    self = NAUTILUS_PATH_BAR (user_data);
    priv = nautilus_path_bar_get_instance_private (self);

    g_return_if_fail (NAUTILUS_IS_FILE (priv->context_menu_file));

    files = g_list_append (NULL, nautilus_file_ref (priv->context_menu_file));

    nautilus_properties_window_present (files, GTK_WIDGET (self), NULL, NULL,
                                        NULL);

    nautilus_file_list_free (files);
}

static void
nautilus_path_bar_init (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;
    GtkBuilder *builder;

    priv = nautilus_path_bar_get_instance_private (self);

    /* Context menu */
    builder = gtk_builder_new_from_resource ("/org/gnome/nautilus/ui/nautilus-pathbar-context-menu.ui");
    priv->current_view_menu = g_object_ref_sink (G_MENU (gtk_builder_get_object (builder, "current-view-menu")));
    priv->button_menu = g_object_ref_sink (G_MENU (gtk_builder_get_object (builder, "button-menu")));
    priv->current_view_menu_popover = g_object_ref_sink (GTK_POPOVER (gtk_popover_new_from_model (NULL,
                                                                                                  G_MENU_MODEL (priv->current_view_menu))));
    priv->button_menu_popover = g_object_ref_sink (GTK_POPOVER (gtk_popover_new_from_model (NULL,
                                                                                            G_MENU_MODEL (priv->button_menu))));
    g_object_unref (builder);

    gtk_widget_set_has_surface (GTK_WIDGET (self), FALSE);
    gtk_widget_set_redraw_on_allocate (GTK_WIDGET (self), FALSE);
    /* Ideally this would be in CSS, but not worth it adding CSS support to
     * the container's logic...
     */
    gtk_widget_set_margin_start (GTK_WIDGET (self), 6);
    gtk_widget_set_margin_end (GTK_WIDGET (self), 6);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                 GTK_STYLE_CLASS_LINKED);
    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                 "path-bar");

    /* Action group */
    priv->action_group = G_ACTION_GROUP (g_simple_action_group_new ());
    g_action_map_add_action_entries (G_ACTION_MAP (priv->action_group),
                                     path_bar_actions,
                                     G_N_ELEMENTS (path_bar_actions),
                                     self);
    gtk_widget_insert_action_group (GTK_WIDGET (self),
                                    "pathbar",
                                    G_ACTION_GROUP (priv->action_group));
}

static void
nautilus_path_bar_finalize (GObject *object)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;

    self = NAUTILUS_PATH_BAR (object);
    priv = nautilus_path_bar_get_instance_private (self);

    g_list_free (priv->button_list);
    g_clear_object (&priv->current_view_menu);
    g_clear_object (&priv->button_menu);
    g_clear_object (&priv->button_menu_popover);
    g_clear_object (&priv->current_view_menu_popover);

    unschedule_pop_up_context_menu (NAUTILUS_PATH_BAR (object));

    G_OBJECT_CLASS (nautilus_path_bar_parent_class)->finalize (object);
}

/* Removes the settings signal handler.  It's safe to call multiple times */
static void
remove_settings_signal (NautilusPathBar *self,
                        GdkDisplay      *display)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (self);

    if (priv->settings_signal_id != 0)
    {
        GtkSettings *settings;

        settings = gtk_settings_get_for_display (display);
        g_signal_handler_disconnect (settings, priv->settings_signal_id);
        priv->settings_signal_id = 0;
    }
}

static void
nautilus_path_bar_dispose (GObject *object)
{
    NautilusPathBar *self;

    self = NAUTILUS_PATH_BAR (object);

    remove_settings_signal (self, gtk_widget_get_display (GTK_WIDGET (object)));

    G_OBJECT_CLASS (nautilus_path_bar_parent_class)->dispose (object);
}

static const char *
get_dir_name (ButtonData *button_data)
{
    switch (button_data->type)
    {
        case ROOT_BUTTON:
        {
            /* Translators: This is the label used in the pathbar when seeing
             * the root directory (also known as /) */
            return _("Computer");
        }

        case ADMIN_ROOT_BUTTON:
        {
            /* Translators: This is the filesystem root directory (also known
             * as /) when seen as administrator */
            return _("Administrator Root");
        }

        case HOME_BUTTON:
        {
            return _("Home");
        }

        case OTHER_LOCATIONS_BUTTON:
        {
            return _("Other Locations");
        }

        case STARRED_BUTTON:
        {
            return _("Starred");
        }

        default:
        {
            return button_data->dir_name;
        }
    }
}

/* We always want to request the same size for the label, whether
 * or not the contents are bold
 */
static void
set_label_size_request (ButtonData *button_data)
{
    gint width, height;
    GtkRequisition nat_req, bold_req;

    if (button_data->label == NULL)
    {
        return;
    }

    gtk_widget_get_preferred_size (button_data->label, NULL, &nat_req);
    gtk_widget_get_preferred_size (button_data->bold_label, &bold_req, NULL);

    width = MAX (nat_req.width, bold_req.width);
    width = MIN (width, NAUTILUS_PATH_BAR_BUTTON_MAX_WIDTH);
    height = MAX (nat_req.height, bold_req.height);

    gtk_widget_set_size_request (button_data->label, width, height);
}

/* Size requisition:
 *
 * Ideally, our size is determined by another widget, and we are just filling
 * available space.
 */
static void
nautilus_path_bar_measure (GtkWidget      *widget,
                           GtkOrientation  orientation,
                           int             for_size,
                           int            *minimum,
                           int            *natural,
                           int            *minimum_baseline,
                           int            *natural_baseline)
{
    NautilusPathBar *self = NULL;
    NautilusPathBarPrivate *priv = NULL;
    ButtonData *button_data = NULL;
    GtkRequisition child_minimum = { 0 };
    GtkRequisition child_natural = { 0 };
    int minimum_size = 0;
    int natural_size = 0;

    self = NAUTILUS_PATH_BAR (widget);
    priv = nautilus_path_bar_get_instance_private (self);

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        for (GList *list = priv->button_list; list != NULL; list = list->next)
        {
            button_data = BUTTON_DATA (list->data);

            set_label_size_request (button_data);

            gtk_widget_get_preferred_size (button_data->container,
                                           &child_minimum, &child_natural);

            if (button_data->type == NORMAL_BUTTON)
            {
                /* Use 2*Height as button width because of ellipsized label.  */
                child_minimum.width = MAX (child_minimum.width, child_minimum.height * 2);
                child_natural.width = MAX (child_minimum.width, child_minimum.height * 2);
            }

            minimum_size = MAX (minimum_size, child_minimum.width);
            natural_size += child_natural.width;
        }
    }
    else
    {
        for (GList *list = priv->button_list; list != NULL; list = list->next)
        {
            button_data = BUTTON_DATA (list->data);
            set_label_size_request (button_data);

            gtk_widget_get_preferred_size (button_data->container,
                                           &child_minimum, &child_natural);

            minimum_size = MAX (minimum_size, child_minimum.height);
            natural_size = MAX (natural_size, child_natural.height);
        }
    }

    if (minimum != NULL)
    {
        *minimum = minimum_size;
    }
    if (natural != NULL)
    {
        *natural = natural_size;
    }
}

static void
nautilus_path_bar_unmap (GtkWidget *widget)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (NAUTILUS_PATH_BAR (widget));

    gdk_window_hide (priv->event_window);

    GTK_WIDGET_CLASS (nautilus_path_bar_parent_class)->unmap (widget);
}

static void
nautilus_path_bar_map (GtkWidget *widget)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (NAUTILUS_PATH_BAR (widget));

    gdk_window_show (priv->event_window);

    GTK_WIDGET_CLASS (nautilus_path_bar_parent_class)->map (widget);
}

#define BUTTON_BOTTOM_SHADOW 1

static void
union_with_clip (GtkWidget *widget,
                 gpointer   clip)
{
    GtkAllocation widget_clip;

    if (!gtk_widget_is_drawable (widget))
    {
        return;
    }

    gtk_widget_get_clip (widget, &widget_clip);

    gdk_rectangle_union (&widget_clip, clip, clip);
}

static void
_set_simple_bottom_clip (GtkWidget *widget,
                         gint       pixels)
{
    GtkAllocation clip;

    gtk_widget_get_allocation (widget, &clip);
    clip.height += pixels;

    gtk_container_forall (GTK_CONTAINER (widget), union_with_clip, &clip);
    gtk_widget_set_clip (widget, &clip);
}

/* This is a tad complicated */
static void
nautilus_path_bar_size_allocate (GtkWidget     *widget,
                                 GtkAllocation *allocation)
{
    GtkWidget *child;
    NautilusPathBarPrivate *priv;
    GtkTextDirection direction;
    GtkAllocation child_allocation;
    GList *list, *first_button;
    gint width;
    gint largest_width;
    GtkRequisition child_requisition;

    priv = nautilus_path_bar_get_instance_private (NAUTILUS_PATH_BAR (widget));

    gtk_widget_set_allocation (widget, allocation);

    if (gtk_widget_get_realized (widget))
    {
        gdk_window_move_resize (priv->event_window,
                                allocation->x, allocation->y,
                                allocation->width, allocation->height);
    }

    /* No path is set so we don't have to allocate anything. */
    if (priv->button_list == NULL)
    {
        _set_simple_bottom_clip (widget, BUTTON_BOTTOM_SHADOW);
        return;
    }
    direction = gtk_widget_get_direction (widget);

    width = 0;

    gtk_widget_get_preferred_size (BUTTON_DATA (priv->button_list->data)->container,
                                   &child_requisition, NULL);
    width += child_requisition.width;

    for (list = priv->button_list->next; list; list = list->next)
    {
        child = BUTTON_DATA (list->data)->button;
        gtk_widget_get_preferred_size (child, &child_requisition, NULL);
        width += child_requisition.width;
    }

    if (width <= allocation->width)
    {
        first_button = g_list_last (priv->button_list);
    }
    else
    {
        gboolean reached_end;
        reached_end = FALSE;

        first_button = priv->button_list;

        /* To see how much space we have, and how many buttons we can display.
         * We start at the first button, count forward until hit the new
         * button, then count backwards.
         */
        /* Count down the path chain towards the end. */
        gtk_widget_get_preferred_size (BUTTON_DATA (first_button->data)->container,
                                       &child_requisition, NULL);
        width = child_requisition.width;
        list = first_button->prev;
        while (list && !reached_end)
        {
            child = BUTTON_DATA (list->data)->container;
            gtk_widget_get_preferred_size (child, &child_requisition, NULL);

            if (width + child_requisition.width > allocation->width)
            {
                reached_end = TRUE;
            }
            else
            {
                width += child_requisition.width;
            }

            list = list->prev;
        }

        /* Finally, we walk up, seeing how many of the previous buttons we can add*/

        while (first_button->next && !reached_end)
        {
            child = BUTTON_DATA (first_button->next->data)->button;
            gtk_widget_get_preferred_size (child, &child_requisition, NULL);

            if (width + child_requisition.width > allocation->width)
            {
                reached_end = TRUE;
            }
            else
            {
                width += child_requisition.width;
                first_button = first_button->next;
            }
        }
    }

    /* Now, we allocate space to the buttons */
    child_allocation.y = allocation->y;
    child_allocation.height = allocation->height;

    if (direction == GTK_TEXT_DIR_RTL)
    {
        child_allocation.x = allocation->x + allocation->width;
    }
    else
    {
        child_allocation.x = allocation->x;
    }

    /* Determine the largest possible allocation size */
    largest_width = allocation->width;
    for (list = first_button; list; list = list->prev)
    {
        child = BUTTON_DATA (list->data)->container;
        gtk_widget_get_preferred_size (child, &child_requisition, NULL);

        child_allocation.width = MIN (child_requisition.width, largest_width);
        if (direction == GTK_TEXT_DIR_RTL)
        {
            child_allocation.x -= child_allocation.width;
        }
        /* Check to see if we've don't have any more space to allocate buttons */

        gtk_widget_set_child_visible (child, TRUE);
        gtk_widget_size_allocate (child, &child_allocation);

        if (direction == GTK_TEXT_DIR_LTR)
        {
            child_allocation.x += child_allocation.width;
        }
    }
    /* Now we go hide all the widgets that don't fit */
    while (list)
    {
        child = BUTTON_DATA (list->data)->container;
        gtk_widget_set_child_visible (child, FALSE);
        list = list->prev;
    }
    for (list = first_button->next; list; list = list->next)
    {
        child = BUTTON_DATA (list->data)->container;
        gtk_widget_set_child_visible (child, FALSE);
    }

    _set_simple_bottom_clip (widget, BUTTON_BOTTOM_SHADOW);
}

static void
nautilus_path_bar_style_updated (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (nautilus_path_bar_parent_class)->style_updated (widget);

    nautilus_path_bar_check_icon_theme (NAUTILUS_PATH_BAR (widget));
}

static void
nautilus_path_bar_display_changed (GtkWidget  *widget,
                                   GdkDisplay *previous_display)
{
    if (GTK_WIDGET_CLASS (nautilus_path_bar_parent_class)->display_changed)
    {
        GTK_WIDGET_CLASS (nautilus_path_bar_parent_class)->display_changed (widget, previous_display);
    }
    /* We might nave a new settings, so we remove the old one */
    if (previous_display != NULL)
    {
        remove_settings_signal (NAUTILUS_PATH_BAR (widget), previous_display);
    }
    nautilus_path_bar_check_icon_theme (NAUTILUS_PATH_BAR (widget));
}

static void
nautilus_path_bar_realize (GtkWidget *widget)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GtkAllocation allocation;
    GdkWindow *window;
    GdkWindowAttr attributes;
    gint attributes_mask;

    gtk_widget_set_realized (widget, TRUE);

    self = NAUTILUS_PATH_BAR (widget);
    priv = nautilus_path_bar_get_instance_private (self);

    window = gtk_widget_get_parent_window (widget);
    gtk_widget_set_window (widget, window);
    g_object_ref (window);

    gtk_widget_get_allocation (widget, &allocation);

    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_ONLY;
    attributes.event_mask = gtk_widget_get_events (widget);
    attributes.event_mask |=
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK;
    attributes_mask = GDK_WA_X | GDK_WA_Y;

    priv->event_window = gdk_window_new (gtk_widget_get_parent_window (widget),
                                         &attributes, attributes_mask);
    gdk_window_set_user_data (priv->event_window, widget);
}

static void
nautilus_path_bar_unrealize (GtkWidget *widget)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;

    self = NAUTILUS_PATH_BAR (widget);
    priv = nautilus_path_bar_get_instance_private (self);

    gdk_window_set_user_data (priv->event_window, NULL);
    gdk_window_destroy (priv->event_window);
    priv->event_window = NULL;

    GTK_WIDGET_CLASS (nautilus_path_bar_parent_class)->unrealize (widget);
}

static void
nautilus_path_bar_add (GtkContainer *container,
                       GtkWidget    *widget)
{
    gtk_widget_set_parent (widget, GTK_WIDGET (container));
}

static void
nautilus_path_bar_remove_1 (GtkContainer *container,
                            GtkWidget    *widget)
{
    gboolean was_visible = gtk_widget_get_visible (widget);
    gtk_widget_unparent (widget);
    if (was_visible)
    {
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
}

static void
nautilus_path_bar_remove (GtkContainer *container,
                          GtkWidget    *widget)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GList *children;

    self = NAUTILUS_PATH_BAR (container);
    priv = nautilus_path_bar_get_instance_private (self);

    children = priv->button_list;
    while (children)
    {
        if (widget == BUTTON_DATA (children->data)->container)
        {
            nautilus_path_bar_remove_1 (container, widget);
            priv->button_list = g_list_remove_link (priv->button_list, children);
            g_list_free_1 (children);
            return;
        }
        children = children->next;
    }
}

static void
nautilus_path_bar_forall (GtkContainer *container,
                          gboolean      include_internals,
                          GtkCallback   callback,
                          gpointer      callback_data)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GList *children;

    g_return_if_fail (callback != NULL);
    self = NAUTILUS_PATH_BAR (container);
    priv = nautilus_path_bar_get_instance_private (self);

    children = priv->button_list;
    while (children)
    {
        GtkWidget *child;
        child = BUTTON_DATA (children->data)->container;
        children = children->next;
        (*callback)(child, callback_data);
    }
}

static GtkWidgetPath *
nautilus_path_bar_get_path_for_child (GtkContainer *container,
                                      GtkWidget    *child)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    GtkWidgetPath *path;

    self = NAUTILUS_PATH_BAR (container);
    priv = nautilus_path_bar_get_instance_private (self);
    path = gtk_widget_path_copy (gtk_widget_get_path (GTK_WIDGET (self)));

    if (gtk_widget_get_visible (child) &&
        gtk_widget_get_child_visible (child))
    {
        GtkWidgetPath *sibling_path;
        GList *visible_children;
        GList *l;
        int pos;

        /* 1. Build the list of visible children, in visually left-to-right order
         * (i.e. independently of the widget's direction).  Note that our
         * button_list is stored in innermost-to-outermost path order!
         */

        visible_children = NULL;

        for (l = priv->button_list; l; l = l->next)
        {
            ButtonData *data = l->data;

            if (gtk_widget_get_visible (data->container) &&
                gtk_widget_get_child_visible (data->container))
            {
                visible_children = g_list_prepend (visible_children, data->container);
            }
        }

        if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
        {
            visible_children = g_list_reverse (visible_children);
        }

        /* 2. Find the index of the child within that list */

        pos = 0;

        for (l = visible_children; l; l = l->next)
        {
            GtkWidget *button = l->data;

            if (button == child)
            {
                break;
            }

            pos++;
        }

        /* 3. Build the path */

        sibling_path = gtk_widget_path_new ();

        for (l = visible_children; l; l = l->next)
        {
            gtk_widget_path_append_for_widget (sibling_path, l->data);
        }

        gtk_widget_path_append_with_siblings (path, sibling_path, pos);

        g_list_free (visible_children);
        gtk_widget_path_unref (sibling_path);
    }
    else
    {
        gtk_widget_path_append_for_widget (path, child);
    }

    return path;
}

static void
nautilus_path_bar_class_init (NautilusPathBarClass *path_bar_class)
{
    GObjectClass *gobject_class;
    GtkWidgetClass *widget_class;
    GtkContainerClass *container_class;

    gobject_class = (GObjectClass *) path_bar_class;
    widget_class = (GtkWidgetClass *) path_bar_class;
    container_class = (GtkContainerClass *) path_bar_class;

    gobject_class->finalize = nautilus_path_bar_finalize;
    gobject_class->dispose = nautilus_path_bar_dispose;

    widget_class->measure = nautilus_path_bar_measure;
    widget_class->realize = nautilus_path_bar_realize;
    widget_class->unrealize = nautilus_path_bar_unrealize;
    widget_class->unmap = nautilus_path_bar_unmap;
    widget_class->map = nautilus_path_bar_map;
    widget_class->size_allocate = nautilus_path_bar_size_allocate;
    widget_class->style_updated = nautilus_path_bar_style_updated;
    widget_class->display_changed = nautilus_path_bar_display_changed;

    container_class->add = nautilus_path_bar_add;
    container_class->forall = nautilus_path_bar_forall;
    container_class->remove = nautilus_path_bar_remove;
    container_class->get_path_for_child = nautilus_path_bar_get_path_for_child;

    path_bar_signals [OPEN_LOCATION] =
        g_signal_new ("open-location",
                      G_OBJECT_CLASS_TYPE (path_bar_class),
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (NautilusPathBarClass, open_location),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_FILE,
                      GTK_TYPE_PLACES_OPEN_FLAGS);
    path_bar_signals [PATH_CLICKED] =
        g_signal_new ("path-clicked",
                      G_OBJECT_CLASS_TYPE (path_bar_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (NautilusPathBarClass, path_clicked),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      G_TYPE_FILE);

    gtk_container_class_handle_border_width (container_class);
}

static void
update_current_view_menu (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (self);
    if (priv->extensions_background_menu != NULL)
    {
        nautilus_gmenu_merge (priv->current_view_menu,
                              priv->extensions_background_menu,
                              "extensions",
                              TRUE);
    }

    if (priv->templates_menu != NULL)
    {
        nautilus_gmenu_merge (priv->current_view_menu, priv->templates_menu,
                              "templates-submenu", TRUE);
    }
}

static void
reset_current_view_menu (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;
    g_autoptr (GtkBuilder) builder = NULL;

    priv = nautilus_path_bar_get_instance_private (self);

    g_clear_object (&priv->current_view_menu);
    builder = gtk_builder_new_from_resource ("/org/gnome/nautilus/ui/nautilus-pathbar-context-menu.ui");
    priv->current_view_menu = g_object_ref_sink (G_MENU (gtk_builder_get_object (builder,
                                                                                 "current-view-menu")));
    gtk_popover_bind_model (priv->current_view_menu_popover,
                            G_MENU_MODEL (priv->current_view_menu), NULL);
}

void
nautilus_path_bar_set_extensions_background_menu (NautilusPathBar *self,
                                                  GMenu           *menu)
{
    NautilusPathBarPrivate *priv;

    g_return_if_fail (NAUTILUS_IS_PATH_BAR (self));

    priv = nautilus_path_bar_get_instance_private (self);
    reset_current_view_menu (self);
    g_clear_object (&priv->extensions_background_menu);
    if (menu != NULL)
    {
        priv->extensions_background_menu = g_object_ref (menu);
    }

    update_current_view_menu (self);
}

void
nautilus_path_bar_set_templates_menu (NautilusPathBar *self,
                                      GMenu           *menu)
{
    NautilusPathBarPrivate *priv;

    g_return_if_fail (NAUTILUS_IS_PATH_BAR (self));

    priv = nautilus_path_bar_get_instance_private (self);
    reset_current_view_menu (self);
    g_clear_object (&priv->templates_menu);
    if (menu != NULL)
    {
        priv->templates_menu = g_object_ref (menu);
    }

    update_current_view_menu (self);
}

/* Changes the icons wherever it is needed */
static void
reload_icons (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;
    GList *list;

    priv = nautilus_path_bar_get_instance_private (self);

    for (list = priv->button_list; list; list = list->next)
    {
        ButtonData *button_data;

        button_data = BUTTON_DATA (list->data);
        if (button_data->type != NORMAL_BUTTON || button_data->is_root)
        {
            nautilus_path_bar_update_button_appearance (button_data);
        }
    }
}

/* Callback used when a GtkSettings value changes */
static void
settings_notify_cb (GObject         *object,
                    GParamSpec      *pspec,
                    NautilusPathBar *self)
{
    const char *name;

    name = g_param_spec_get_name (pspec);

    if (!strcmp (name, "gtk-icon-theme-name") || !strcmp (name, "gtk-icon-sizes"))
    {
        reload_icons (self);
    }
}

static void
nautilus_path_bar_check_icon_theme (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;
    GtkSettings *settings;

    priv = nautilus_path_bar_get_instance_private (self);

    if (priv->settings_signal_id)
    {
        return;
    }

    settings = gtk_settings_get_for_display (gtk_widget_get_display (GTK_WIDGET (self)));
    priv->settings_signal_id = g_signal_connect (settings, "notify", G_CALLBACK (settings_notify_cb), self);

    reload_icons (self);
}

static void
button_data_free (ButtonData *button_data)
{
    g_object_unref (button_data->path);
    g_free (button_data->dir_name);
    if (button_data->file != NULL)
    {
        g_signal_handler_disconnect (button_data->file,
                                     button_data->file_changed_signal_id);
        nautilus_file_monitor_remove (button_data->file, button_data);
        nautilus_file_unref (button_data->file);
    }

    g_free (button_data);
}

/* Public functions and their helpers */
static void
nautilus_path_bar_clear_buttons (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (self);

    while (priv->button_list != NULL)
    {
        ButtonData *button_data;

        button_data = BUTTON_DATA (priv->button_list->data);

        gtk_container_remove (GTK_CONTAINER (self), button_data->container);

        button_data_free (button_data);
    }
}

static gboolean
button_clicked_cb (GtkWidget *button,
                   GdkEvent *event,
                   gpointer   data)
{
    ButtonData *button_data;
    NautilusPathBarPrivate *priv;
    NautilusPathBar *self;
    GdkModifierType state;

    button_data = BUTTON_DATA (data);
    if (button_data->ignore_changes)
    {
        return GDK_EVENT_STOP;
    }

    self = button_data->path_bar;
    priv = nautilus_path_bar_get_instance_private (self);

    gdk_event_get_state (event, &state);

    if ((state & GDK_CONTROL_MASK) != 0)
    {
        g_signal_emit (button_data->path_bar, path_bar_signals[OPEN_LOCATION], 0,
                       button_data->path,
                       GTK_PLACES_OPEN_NEW_WINDOW);
    }
    else
    {
        if (g_file_equal (button_data->path, priv->current_path))
        {
            gtk_popover_popup (priv->current_view_menu_popover);
        }
        else if (((GdkEventButton *) event)->button == GDK_BUTTON_SECONDARY)
        {
            gtk_popover_set_relative_to (priv->button_menu_popover, button);
            pop_up_pathbar_context_menu (self, button_data->file);
        }
        else
        {
            g_signal_emit (self, path_bar_signals[OPEN_LOCATION], 0,
                           button_data->path,
                           0);
        }
    }

    return GDK_EVENT_STOP;
}

static void
real_pop_up_pathbar_context_menu (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (self);

    gtk_popover_popup (priv->button_menu_popover);
}

static void
pathbar_popup_file_attributes_ready (NautilusFile *file,
                                     gpointer      data)
{
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;

    self = data;

    g_return_if_fail (NAUTILUS_IS_PATH_BAR (self));

    priv = nautilus_path_bar_get_instance_private (self);

    g_return_if_fail (file == priv->context_menu_file);

    real_pop_up_pathbar_context_menu (self);
}

static void
unschedule_pop_up_context_menu (NautilusPathBar *self)
{
    NautilusPathBarPrivate *priv;

    priv = nautilus_path_bar_get_instance_private (self);

    if (priv->context_menu_file)
    {
        g_return_if_fail (NAUTILUS_IS_FILE (priv->context_menu_file));
        nautilus_file_cancel_call_when_ready (priv->context_menu_file,
                                              pathbar_popup_file_attributes_ready,
                                              self);
        g_clear_pointer (&priv->context_menu_file, nautilus_file_unref);
    }
}

static void
schedule_pop_up_context_menu (NautilusPathBar *self,
                              NautilusFile    *file)
{
    NautilusPathBarPrivate *priv;

    g_return_if_fail (NAUTILUS_IS_FILE (file));

    priv = nautilus_path_bar_get_instance_private (self);

    if (file == priv->context_menu_file)
    {
        if (nautilus_file_check_if_ready (file,
                                          NAUTILUS_FILE_ATTRIBUTE_INFO |
                                          NAUTILUS_FILE_ATTRIBUTE_MOUNT |
                                          NAUTILUS_FILE_ATTRIBUTE_FILESYSTEM_INFO))
        {
            real_pop_up_pathbar_context_menu (self);
        }
    }
    else
    {
        unschedule_pop_up_context_menu (self);

        priv->context_menu_file = nautilus_file_ref (file);
        nautilus_file_call_when_ready (priv->context_menu_file,
                                       NAUTILUS_FILE_ATTRIBUTE_INFO |
                                       NAUTILUS_FILE_ATTRIBUTE_MOUNT |
                                       NAUTILUS_FILE_ATTRIBUTE_FILESYSTEM_INFO,
                                       pathbar_popup_file_attributes_ready,
                                       self);
    }
}

static void
pop_up_pathbar_context_menu (NautilusPathBar *self,
                             NautilusFile    *file)
{
    if (file)
    {
        schedule_pop_up_context_menu (self, file);
    }
}


static void
on_multi_press_gesture_pressed (GtkGestureMultiPress *gesture,
                                gint                  n_press,
                                gdouble               x,
                                gdouble               y,
                                gpointer              user_data)
{
    GdkEventSequence *sequence;
    const GdkEvent *event;
    GdkModifierType state;

    if (n_press != 1)
    {
        return;
    }

    sequence = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (gesture));
    event = gtk_gesture_get_last_event (GTK_GESTURE (gesture), sequence);

    gdk_event_get_state (event, &state);

    state &= gtk_accelerator_get_default_mod_mask ();

    if (state == 0)
    {
        ButtonData *button_data;

        button_data = BUTTON_DATA (user_data);

        g_signal_emit (button_data->path_bar, path_bar_signals[OPEN_LOCATION], 0,
                       button_data->path,
                       GTK_PLACES_OPEN_NEW_TAB);
    }
}

static GIcon *
get_gicon_for_mount (ButtonData *button_data)
{
    GIcon *icon;
    GMount *mount;

    icon = NULL;
    mount = nautilus_get_mounted_mount_for_root (button_data->path);

    if (mount != NULL)
    {
        icon = g_mount_get_symbolic_icon (mount);
        g_object_unref (mount);
    }

    return icon;
}

static GIcon *
get_gicon (ButtonData *button_data)
{
    switch (button_data->type)
    {
        case ROOT_BUTTON:
        case ADMIN_ROOT_BUTTON:
        {
            return g_themed_icon_new (NAUTILUS_ICON_FILESYSTEM);
        }

        case HOME_BUTTON:
        {
            return g_themed_icon_new (NAUTILUS_ICON_HOME);
        }

        case MOUNT_BUTTON:
        {
            return get_gicon_for_mount (button_data);
        }

        case STARRED_BUTTON:
        {
            return g_themed_icon_new ("starred-symbolic");
        }

        case RECENT_BUTTON:
        {
            return g_themed_icon_new ("document-open-recent-symbolic");
        }

        case OTHER_LOCATIONS_BUTTON:
        {
            return g_themed_icon_new ("list-add-symbolic");
        }

        default:
            return NULL;
    }

    return NULL;
}

static void
nautilus_path_bar_update_button_appearance (ButtonData *button_data)
{
    const gchar *dir_name = get_dir_name (button_data);
    GIcon *icon;

    if (button_data->label != NULL)
    {
        char *markup;

        markup = g_markup_printf_escaped ("<b>%s</b>", dir_name);

        if (gtk_label_get_use_markup (GTK_LABEL (button_data->label)))
        {
            gtk_label_set_markup (GTK_LABEL (button_data->label), markup);
        }
        else
        {
            gtk_label_set_text (GTK_LABEL (button_data->label), dir_name);
        }

        gtk_label_set_markup (GTK_LABEL (button_data->bold_label), markup);
        g_free (markup);
    }

    icon = get_gicon (button_data);
    if (icon != NULL)
    {
        gtk_image_set_from_gicon (GTK_IMAGE (button_data->image), icon, GTK_ICON_SIZE_MENU);
        gtk_style_context_add_class (gtk_widget_get_style_context (button_data->button),
                                     "image-button");
        gtk_widget_show (GTK_WIDGET (button_data->image));
        g_object_unref (icon);
    }
    else
    {
        gtk_widget_hide (GTK_WIDGET (button_data->image));
        gtk_style_context_remove_class (gtk_widget_get_style_context (button_data->button),
                                        "image-button");
    }
}

static void
nautilus_path_bar_update_button_state (ButtonData *button_data,
                                       gboolean    current_dir)
{
    if (button_data->label != NULL)
    {
        gtk_label_set_label (GTK_LABEL (button_data->label), NULL);
        gtk_label_set_label (GTK_LABEL (button_data->bold_label), NULL);
        gtk_label_set_use_markup (GTK_LABEL (button_data->label), current_dir);
    }

    nautilus_path_bar_update_button_appearance (button_data);

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button_data->button)) != current_dir)
    {
        button_data->ignore_changes = TRUE;
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_data->button), current_dir);
        button_data->ignore_changes = FALSE;
    }
}

static void
setup_button_type (ButtonData      *button_data,
                   NautilusPathBar *self,
                   GFile           *location)
{
    g_autoptr (GMount) mount = NULL;
    g_autofree gchar *uri = NULL;

    if (nautilus_is_root_directory (location))
    {
        button_data->type = ROOT_BUTTON;
    }
    else if (nautilus_is_home_directory (location))
    {
        button_data->type = HOME_BUTTON;
        button_data->is_root = TRUE;
    }
    else if (nautilus_is_recent_directory (location))
    {
        button_data->type = RECENT_BUTTON;
        button_data->is_root = TRUE;
    }
    else if (nautilus_is_starred_directory (location))
    {
        button_data->type = STARRED_BUTTON;
        button_data->is_root = TRUE;
    }
    else if ((mount = nautilus_get_mounted_mount_for_root (location)) != NULL)
    {
        button_data->dir_name = g_mount_get_name (mount);
        button_data->type = MOUNT_BUTTON;
        button_data->is_root = TRUE;
    }
    else if (nautilus_is_other_locations_directory (location))
    {
        button_data->type = OTHER_LOCATIONS_BUTTON;
        button_data->is_root = TRUE;
    }
    else if (strcmp ((uri = g_file_get_uri (location)), "admin:///") == 0)
    {
        button_data->type = ADMIN_ROOT_BUTTON;
        button_data->is_root = TRUE;
    }
    else
    {
        button_data->type = NORMAL_BUTTON;
    }
}

static void
button_data_file_changed (NautilusFile *file,
                          ButtonData   *button_data)
{
    GFile *location, *current_location, *parent, *button_parent;
    ButtonData *current_button_data;
    char *display_name;
    NautilusPathBar *self;
    NautilusPathBarPrivate *priv;
    gboolean renamed, child;

    self = (NautilusPathBar *) gtk_widget_get_ancestor (button_data->button,
                                                        NAUTILUS_TYPE_PATH_BAR);
    priv = nautilus_path_bar_get_instance_private (self);

    if (self == NULL)
    {
        return;
    }

    g_return_if_fail (priv->current_path != NULL);
    g_return_if_fail (priv->current_button_data != NULL);

    current_button_data = priv->current_button_data;

    location = nautilus_file_get_location (file);
    if (!g_file_equal (button_data->path, location))
    {
        parent = g_file_get_parent (location);
        button_parent = g_file_get_parent (button_data->path);

        renamed = (parent != NULL && button_parent != NULL) &&
                  g_file_equal (parent, button_parent);

        if (parent != NULL)
        {
            g_object_unref (parent);
        }
        if (button_parent != NULL)
        {
            g_object_unref (button_parent);
        }

        if (renamed)
        {
            button_data->path = g_object_ref (location);
        }
        else
        {
            /* the file has been moved.
             * If it was below the currently displayed location, remove it.
             * If it was not below the currently displayed location, update the path bar
             */
            child = g_file_has_prefix (button_data->path,
                                       priv->current_path);

            if (child)
            {
                /* moved file inside current path hierarchy */
                g_object_unref (location);
                location = g_file_get_parent (button_data->path);
                current_location = g_object_ref (priv->current_path);
            }
            else
            {
                /* moved current path, or file outside current path hierarchy.
                 * Update path bar to new locations.
                 */
                current_location = nautilus_file_get_location (current_button_data->file);
            }

            nautilus_path_bar_update_path (self, location);
            nautilus_path_bar_set_path (self, current_location);
            g_object_unref (location);
            g_object_unref (current_location);
            return;
        }
    }
    else if (nautilus_file_is_gone (file))
    {
        gint idx, position;

        /* if the current or a parent location are gone, clear all the buttons,
         * the view will set the new path.
         */
        current_location = nautilus_file_get_location (current_button_data->file);

        if (g_file_has_prefix (current_location, location) ||
            g_file_equal (current_location, location))
        {
            nautilus_path_bar_clear_buttons (self);
        }
        else if (g_file_has_prefix (location, current_location))
        {
            /* remove this and the following buttons */
            position = g_list_position (priv->button_list,
                                        g_list_find (priv->button_list, button_data));

            if (position != -1)
            {
                for (idx = 0; idx <= position; idx++)
                {
                    ButtonData *data;

                    data = BUTTON_DATA (priv->button_list->data);

                    gtk_container_remove (GTK_CONTAINER (self), data->container);

                    button_data_free (data);
                }
            }
        }

        g_object_unref (current_location);
        g_object_unref (location);
        return;
    }
    g_object_unref (location);

    /* MOUNTs use the GMount as the name, so don't update for those */
    if (button_data->type != MOUNT_BUTTON)
    {
        display_name = nautilus_file_get_display_name (file);
        if (g_strcmp0 (display_name, button_data->dir_name) != 0)
        {
            g_free (button_data->dir_name);
            button_data->dir_name = g_strdup (display_name);
        }

        g_free (display_name);
    }
    nautilus_path_bar_update_button_appearance (button_data);
}

static ButtonData *
make_button_data (NautilusPathBar *self,
                  NautilusFile    *file,
                  gboolean         current_dir)
{
    GFile *path;
    GtkWidget *child;
    ButtonData *button_data;
    NautilusPathBarPrivate *priv;
    GtkGesture *gesture;

    priv = nautilus_path_bar_get_instance_private (self);
    path = nautilus_file_get_location (file);
    child = NULL;

    /* Is it a special button? */
    button_data = g_new0 (ButtonData, 1);

    setup_button_type (button_data, self, path);
    button_data->button = gtk_toggle_button_new ();
    gtk_style_context_add_class (gtk_widget_get_style_context (button_data->button),
                                 "text-button");
    gtk_widget_set_focus_on_click (button_data->button, FALSE);
    /* TODO update button type when xdg directories change */

    button_data->image = gtk_image_new ();

    switch (button_data->type)
    {
        case ROOT_BUTTON:
        case ADMIN_ROOT_BUTTON:
        {
            child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
            button_data->label = gtk_label_new (NULL);
            button_data->disclosure_arrow = gtk_image_new_from_icon_name ("pan-down-symbolic",
                                                                          GTK_ICON_SIZE_MENU);
            button_data->container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_box_pack_start (GTK_BOX (button_data->container), button_data->button);

            gtk_box_pack_start (GTK_BOX (child), button_data->image);
            gtk_box_pack_start (GTK_BOX (child), button_data->label);
            gtk_box_pack_start (GTK_BOX (child), button_data->disclosure_arrow);
        }
        break;

        case HOME_BUTTON:
        case MOUNT_BUTTON:
        case RECENT_BUTTON:
        case STARRED_BUTTON:
        case OTHER_LOCATIONS_BUTTON:
        {
            button_data->label = gtk_label_new (NULL);
            child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
            button_data->disclosure_arrow = gtk_image_new_from_icon_name ("pan-down-symbolic",
                                                                          GTK_ICON_SIZE_MENU);
            button_data->container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_box_pack_start (GTK_BOX (button_data->container), button_data->button);

            gtk_box_pack_start (GTK_BOX (child), button_data->image);
            gtk_box_pack_start (GTK_BOX (child), button_data->label);
            gtk_box_pack_start (GTK_BOX (child), button_data->disclosure_arrow);
        }
        break;

        case NORMAL_BUTTON:
        /* Fall through */
        default:
        {
            GtkWidget *separator_label;

            separator_label = gtk_label_new (G_DIR_SEPARATOR_S);
            gtk_style_context_add_class (gtk_widget_get_style_context (separator_label), "dim-label");
            button_data->label = gtk_label_new (NULL);
            child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
            button_data->disclosure_arrow = gtk_image_new_from_icon_name ("pan-down-symbolic",
                                                                          GTK_ICON_SIZE_MENU);
            button_data->container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_pack_start (GTK_BOX (button_data->container), separator_label);
            gtk_box_pack_start (GTK_BOX (button_data->container), button_data->button);

            gtk_box_pack_start (GTK_BOX (child), button_data->label);
            gtk_box_pack_start (GTK_BOX (child), button_data->disclosure_arrow);
        }
        break;
    }

    gtk_widget_set_visible (button_data->disclosure_arrow, current_dir);
    if (current_dir)
    {
        gtk_popover_set_relative_to (priv->current_view_menu_popover, button_data->button);
    }

    if (button_data->label != NULL)
    {
        gtk_label_set_ellipsize (GTK_LABEL (button_data->label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_single_line_mode (GTK_LABEL (button_data->label), TRUE);

        button_data->bold_label = gtk_label_new (NULL);
        gtk_widget_hide (button_data->bold_label);
        gtk_label_set_single_line_mode (GTK_LABEL (button_data->bold_label), TRUE);
        gtk_box_pack_start (GTK_BOX (child), button_data->bold_label);
    }

    if (button_data->path == NULL)
    {
        button_data->path = g_object_ref (path);
    }
    if (button_data->dir_name == NULL)
    {
        button_data->dir_name = nautilus_file_get_display_name (file);
    }
    if (button_data->file == NULL)
    {
        button_data->file = nautilus_file_ref (file);
        nautilus_file_monitor_add (button_data->file, button_data,
                                   NAUTILUS_FILE_ATTRIBUTES_FOR_ICON);
        button_data->file_changed_signal_id =
            g_signal_connect (button_data->file, "changed",
                              G_CALLBACK (button_data_file_changed),
                              button_data);
    }

    gtk_container_add (GTK_CONTAINER (button_data->button), child);

    nautilus_path_bar_update_button_state (button_data, current_dir);

    button_data->path_bar = self;

    g_signal_connect (button_data->button, "button-press-event", G_CALLBACK (button_clicked_cb), button_data);

    /* A gesture is needed here, because GtkButton doesn’t react to middle-clicking.
     */
    gesture = gtk_gesture_multi_press_new ();

    gtk_widget_add_controller (button_data->button, GTK_EVENT_CONTROLLER (gesture));

    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_MIDDLE);

    g_signal_connect (gesture, "pressed",
                      G_CALLBACK (on_multi_press_gesture_pressed), button_data);

    nautilus_drag_slot_proxy_init (button_data->button, button_data->file, NULL);

    g_object_unref (path);

    return button_data;
}

static void
nautilus_path_bar_update_path (NautilusPathBar *self,
                               GFile           *file_path)
{
    NautilusFile *file;
    NautilusPathBarPrivate *priv;
    gboolean first_directory;
    GList *new_buttons, *l;
    ButtonData *button_data;

    g_return_if_fail (NAUTILUS_IS_PATH_BAR (self));
    g_return_if_fail (file_path != NULL);

    priv = nautilus_path_bar_get_instance_private (self);
    first_directory = TRUE;
    new_buttons = NULL;

    file = nautilus_file_get (file_path);

    while (file != NULL)
    {
        NautilusFile *parent_file;

        parent_file = nautilus_file_get_parent (file);
        button_data = make_button_data (self, file, first_directory);
        nautilus_file_unref (file);

        if (first_directory)
        {
            first_directory = FALSE;
        }

        new_buttons = g_list_prepend (new_buttons, button_data);

        if (parent_file != NULL &&
            button_data->is_root)
        {
            nautilus_file_unref (parent_file);
            break;
        }

        file = parent_file;
    }

    nautilus_path_bar_clear_buttons (self);
    priv->button_list = g_list_reverse (new_buttons);

    for (l = priv->button_list; l; l = l->next)
    {
        GtkWidget *container;
        container = BUTTON_DATA (l->data)->container;
        gtk_container_add (GTK_CONTAINER (self), container);
    }
}

void
nautilus_path_bar_set_path (NautilusPathBar *self,
                            GFile           *file_path)
{
    ButtonData *button_data;
    NautilusPathBarPrivate *priv;

    g_return_if_fail (NAUTILUS_IS_PATH_BAR (self));
    g_return_if_fail (file_path != NULL);

    priv = nautilus_path_bar_get_instance_private (self);

    /* Check whether the new path is already present in the pathbar as buttons.
     * This could be a parent directory or a previous selected subdirectory. */
    nautilus_path_bar_update_path (self, file_path);
    button_data = g_list_nth_data (priv->button_list, 0);

    if (priv->current_path != NULL)
    {
        g_object_unref (priv->current_path);
    }

    priv->current_path = g_object_ref (file_path);
    priv->current_button_data = button_data;
}
