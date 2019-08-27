/*
 * nautilus-previewer: nautilus previewer DBus wrapper
 *
 * Copyright (C) 2011, Red Hat, Inc.
 *
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Cosimo Cecchi <cosimoc@redhat.com>
 *
 */

#include "config.h"

#include "nautilus-previewer.h"

#include "nautilus-files-view.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

#define DEBUG_FLAG NAUTILUS_DEBUG_PREVIEWER
#include "nautilus-debug.h"

#include <gio/gio.h>

#define PREVIEWER_DBUS_NAME "org.gnome.NautilusPreviewer"
#define PREVIEWER2_DBUS_IFACE "org.gnome.NautilusPreviewer2"
#define PREVIEWER_DBUS_PATH "/org/gnome/NautilusPreviewer"

static void
previewer2_method_ready_cb (GObject      *source,
                            GAsyncResult *res,
                            gpointer      user_data)
{
    GDBusConnection *connection = G_DBUS_CONNECTION (source);
    g_autoptr(GError) error = NULL;

    g_dbus_connection_call_finish (connection, res, &error);

    if (error != NULL)
    {
        DEBUG ("Unable to call method on NautilusPreviewer: %s", error->message);
    }
}

void
nautilus_previewer_call_show_file (const gchar *uri,
                                   const gchar *window_handle,
                                   guint        xid,
                                   gboolean     close_if_already_visible)
{
    GDBusConnection *connection = g_application_get_dbus_connection (g_application_get_default ());
    GVariant *variant;

    variant = g_variant_new ("(ssb)",
                             uri, window_handle, close_if_already_visible);

    g_dbus_connection_call (connection,
                            PREVIEWER_DBUS_NAME,
                            PREVIEWER_DBUS_PATH,
                            PREVIEWER2_DBUS_IFACE,
                            "ShowFile",
                            variant,
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            previewer2_method_ready_cb,
                            NULL);
}

void
nautilus_previewer_call_close (void)
{
    GDBusConnection *connection = g_application_get_dbus_connection (g_application_get_default ());

    /* don't autostart the previewer if it's not running */
    g_dbus_connection_call (connection,
                            PREVIEWER_DBUS_NAME,
                            PREVIEWER_DBUS_PATH,
                            PREVIEWER2_DBUS_IFACE,
                            "Close",
                            NULL,
                            NULL,
                            G_DBUS_CALL_FLAGS_NO_AUTO_START,
                            -1,
                            NULL,
                            previewer2_method_ready_cb,
                            NULL);
}

static void
previewer_selection_event (GDBusConnection *connection,
                           const gchar *sender_name,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *signal_name,
                           GVariant *parameters,
                           gpointer user_data)
{
    GApplication *application = g_application_get_default ();
    GList *l, *windows = gtk_application_get_windows (GTK_APPLICATION (application));
    NautilusWindow *window = NULL;
    NautilusWindowSlot *slot;
    NautilusView *view;
    GtkDirectionType direction;

    for (l = windows; l != NULL; l = l->next)
    {
        if (NAUTILUS_IS_WINDOW (l->data))
        {
            window = l->data;
            break;
        }
    }

    if (window == NULL)
    {
        return;
    }

    slot = nautilus_window_get_active_slot (window);
    view = nautilus_window_slot_get_current_view (slot);

    if (!NAUTILUS_IS_FILES_VIEW (view))
    {
        return;
    }

    g_variant_get (parameters, "(u)", &direction);
    nautilus_files_view_preview_selection_event (NAUTILUS_FILES_VIEW (view), direction);
}

guint
nautilus_previewer_connect_selection_event (GDBusConnection *connection)
{
    return g_dbus_connection_signal_subscribe (connection,
                                               PREVIEWER_DBUS_NAME,
                                               PREVIEWER2_DBUS_IFACE,
                                               "SelectionEvent",
                                               PREVIEWER_DBUS_PATH,
                                               NULL,
                                               G_DBUS_SIGNAL_FLAGS_NONE,
                                               previewer_selection_event,
                                               NULL,
                                               NULL);
}

void
nautilus_previewer_disconnect_selection_event (GDBusConnection *connection,
                                               guint            event_id)
{
    g_dbus_connection_signal_unsubscribe (connection, event_id);
}
