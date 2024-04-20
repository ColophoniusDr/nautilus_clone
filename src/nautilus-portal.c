/*
 * Copyright (C) 2024 GNOME Foundation Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Original Author: António Fernandes <antoniof@gnome.org>
 */

#define G_LOG_DOMAIN "nautilus-dbus"

#include "nautilus-portal.h"

#include <config.h>
#include <xdp-gnome/externalwindow.h>
#include <xdp-gnome/request.h>
#include <xdp-gnome/xdg-desktop-portal-dbus.h>

#include "nautilus-window.h"

#define DESKTOP_PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"

/* https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html#org-freedesktop-portal-request-response
 */
#define RESPONSE_SUCCESS 0
#define RESPONSE_USER_CANCELLED 1
#define RESPONSE_OTHER 2

struct _NautilusPortal
{
    GObject parent;

    XdpImplFileChooser *implementation_skeleton;
};

G_DEFINE_TYPE (NautilusPortal, nautilus_portal, G_TYPE_OBJECT);

typedef struct {
    NautilusPortal *self;

    GDBusMethodInvocation *invocation;
    Request *request;

    ExternalWindow *external_parent;
    GtkWindow *window;
} FileChooserData;

static void
file_chooser_data_free (gpointer data)
{
    FileChooserData *fc_data = data;

    if (fc_data->request != NULL)
    {
        g_signal_handlers_disconnect_by_data (fc_data->request, fc_data);
    }
    g_clear_object (&fc_data->request);
    g_clear_object (&fc_data->external_parent);

    if (fc_data->window != NULL)
    {
        g_signal_handlers_disconnect_by_data (fc_data->window, fc_data);
    }
    g_clear_weak_pointer (&fc_data->window);

    g_free (fc_data);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (FileChooserData, file_chooser_data_free)

static void
complete_file_chooser (FileChooserData *data,
                       int              response)
{
    const char *method_name = g_dbus_method_invocation_get_method_name (data->invocation);
    GVariantBuilder opt_builder;

    g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

    if (strcmp (method_name, "OpenFile") == 0)
    {
        xdp_impl_file_chooser_complete_open_file (data->self->implementation_skeleton,
                                                  data->invocation,
                                                  response,
                                                  g_variant_builder_end (&opt_builder));
    }
    else if (strcmp (method_name, "SaveFile") == 0)
    {
        xdp_impl_file_chooser_complete_save_file (data->self->implementation_skeleton,
                                                  data->invocation,
                                                  response,
                                                  g_variant_builder_end (&opt_builder));
    }
    else if (strcmp (method_name, "SaveFiles") == 0)
    {
        xdp_impl_file_chooser_complete_save_files (data->self->implementation_skeleton,
                                                   data->invocation,
                                                   response,
                                                   g_variant_builder_end (&opt_builder));
    }
    else
    {
        g_assert_not_reached ();
    }

    request_unexport (data->request);

    gtk_window_destroy (data->window);
}

static void
on_window_close_request (gpointer user_data)
{
    g_autoptr (FileChooserData) data = user_data;

    complete_file_chooser (data, RESPONSE_USER_CANCELLED);
}

static gboolean
handle_close (XdpImplRequest        *request,
              GDBusMethodInvocation *invocation,
              gpointer               user_data)
{
    g_autoptr (FileChooserData) data = (FileChooserData *) user_data;

    complete_file_chooser (data, RESPONSE_OTHER);

    xdp_impl_request_complete_close (request, invocation);

    return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_open_file (XdpImplFileChooser    *object,
                  GDBusMethodInvocation *invocation,
                  const char            *arg_handle,
                  const char            *arg_app_id,
                  const char            *arg_parent_window,
                  const char            *arg_title,
                  GVariant              *arg_options,
                  gpointer               user_data)
{
    NautilusPortal *self = NAUTILUS_PORTAL (user_data);
    FileChooserData *data = g_new0 (FileChooserData, 1);
    g_autoptr (GFile) starting_location = NULL;
    const char *sender = g_dbus_method_invocation_get_sender (invocation);
    gboolean modal = TRUE;
    const char *path;

    data->self = self;
    data->invocation = invocation;
    data->request = request_new (sender, arg_app_id, arg_handle);
    g_signal_connect (data->request, "handle-close",
                      G_CALLBACK (handle_close), data);

    (void) g_variant_lookup (arg_options, "modal", "b", &modal);

    if (arg_parent_window != NULL)
    {
        data->external_parent = create_external_window_from_handle (arg_parent_window);
        if (data->external_parent == NULL)
        {
            g_warning ("Failed to associate portal window with parent window %s",
                       arg_parent_window);
        }
    }

    if (!g_variant_lookup (arg_options, "current_folder", "^&ay", &path))
    {
        path = g_get_home_dir ();
    }
    starting_location = g_file_new_for_path (path);

    NautilusWindow *window = nautilus_window_new ();

    nautilus_window_open_location_full (window,
                                        starting_location,
                                        NAUTILUS_OPEN_FLAG_NORMAL, NULL, NULL);

    g_set_weak_pointer (&data->window, GTK_WINDOW (window));

    g_signal_connect_swapped (window, "close-request",
                              G_CALLBACK (on_window_close_request), data);

    gtk_widget_realize (GTK_WIDGET (data->window));

    if (data->external_parent != NULL)
    {
        GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));

        external_window_set_parent_of (data->external_parent, surface);
        gtk_window_set_modal (data->window, modal);
    }

    gtk_window_present (data->window);

    request_export (data->request, g_dbus_method_invocation_get_connection (data->invocation));

    return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
nautilus_portal_dispose (GObject *object)
{
    NautilusPortal *self = (NautilusPortal *) object;

    g_signal_handlers_disconnect_by_data (self->implementation_skeleton, self);
    g_clear_object (&self->implementation_skeleton);

    G_OBJECT_CLASS (nautilus_portal_parent_class)->dispose (object);
}

static void
nautilus_portal_class_init (NautilusPortalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = nautilus_portal_dispose;
}

static void
nautilus_portal_init (NautilusPortal *self)
{
    self->implementation_skeleton = xdp_impl_file_chooser_skeleton_new ();
}

NautilusPortal *
nautilus_portal_new (void)
{
    return g_object_new (NAUTILUS_TYPE_PORTAL, NULL);
}

gboolean
nautilus_portal_register (NautilusPortal   *self,
                          GDBusConnection  *connection,
                          GError          **error)
{
    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->implementation_skeleton),
                                           connection,
                                           DESKTOP_PORTAL_OBJECT_PATH,
                                           error))
    {
        return FALSE;
    }

    g_signal_connect (self->implementation_skeleton, "handle-open-file",
                      G_CALLBACK (handle_open_file), self);
    g_signal_connect (self->implementation_skeleton, "handle-save-file",
                      G_CALLBACK (handle_open_file), self);
    g_signal_connect (self->implementation_skeleton, "handle-save-files",
                      G_CALLBACK (handle_open_file), self);

    return TRUE;
}

void
nautilus_portal_unregister (NautilusPortal *self)
{
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->implementation_skeleton));
}
