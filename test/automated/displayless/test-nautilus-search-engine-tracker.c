#include <src/nautilus-file-utilities.h>
#include <src/nautilus-global-preferences.h>
#include <src/nautilus-search-provider.h>
#include <src/nautilus-search-engine.c>
#include <gtk/gtk.h>
#include <locale.h>
#include "test-utilities.h"
#include <tracker-sparql.h>
#include <glib/gprintf.h>

static void
hits_added_cb (NautilusSearchEngine *engine,
               GSList               *hits)
{
    g_print ("Hits added for search engine tracker!\n");
    for (gint hit_number = 0; hits != NULL; hits = hits->next, hit_number++)
    {
        g_print ("Hit %i: %s\n", hit_number, nautilus_search_hit_get_uri (hits->data));
    }
}

static void
finished_cb (NautilusSearchEngine         *engine,
             NautilusSearchProviderStatus  status,
             gpointer                      user_data)
{
    TrackerSparqlConnection *connection;
    g_autofree gchar *sparql_query;

    nautilus_search_provider_stop (NAUTILUS_SEARCH_PROVIDER (engine));

    g_print ("\nNautilus search engine tracker finished!\n");

    connection = tracker_sparql_connection_get (NULL, NULL);
    sparql_query = g_strdup_printf ("DELETE WHERE { <nautilus-test-tracker> ?p ?o }");
    tracker_sparql_connection_update (connection,
                                      sparql_query,
                                      0,
                                      NULL,
                                      NULL);

    g_main_loop_quit (user_data);
}

int
main (int   argc,
      char *argv[])
{
    g_autoptr (GMainLoop) loop = NULL;
    NautilusSearchEngine *engine;
    NautilusSearchEngineModel *model;
    NautilusSearchEnginePrivate *priv;
    g_autoptr (NautilusDirectory) directory = NULL;
    g_autoptr (NautilusQuery) query = NULL;
    g_autoptr (GFile) location = NULL;
    TrackerSparqlConnection *connection;
    g_autofree gchar *sparql_query;

    connection = tracker_sparql_connection_get (NULL, NULL);

    loop = g_main_loop_new (NULL, TRUE);

    nautilus_ensure_extension_points ();
    /* Needed for nautilus-query.c. */
    nautilus_global_preferences_init ();

    engine = nautilus_search_engine_new ();
    priv = nautilus_search_engine_get_instance_private (engine);
    g_signal_connect (engine, "hits-added",
                      G_CALLBACK (hits_added_cb), NULL);
    g_signal_connect (engine, "finished",
                      G_CALLBACK (finished_cb), loop);

    query = nautilus_query_new ();
    nautilus_query_set_text (query, "target");
    nautilus_search_provider_set_query (NAUTILUS_SEARCH_PROVIDER (engine), query);

    location = g_file_new_for_path (g_get_tmp_dir ());
    directory = nautilus_directory_get (location);
    nautilus_query_set_location (query, location);

    model = nautilus_search_engine_get_model_provider (engine);
    nautilus_search_engine_model_set_model (model, directory);

    sparql_query = g_strdup_printf ("INSERT DATA {\n<nautilus-test-tracker> ");
    sparql_query = g_strconcat (sparql_query, "a nfo:FileDataObject ;", NULL);
    sparql_query = g_strconcat (sparql_query, "\na nie:InformationElement ;", NULL);
    sparql_query = g_strconcat (sparql_query, "\nnie:url 'file:///tmp/target_file';", NULL);
    sparql_query = g_strconcat (sparql_query, "\nnie:mimeType 'text/plain';", NULL);
    sparql_query = g_strconcat (sparql_query, "\nnfo:fileName 'target';", NULL);
    sparql_query = g_strconcat (sparql_query, "\nnfo:fileLastModified '2001-01-01T00:00:01Z';", NULL);
    sparql_query = g_strconcat (sparql_query, "\nnfo:fileLastAccessed '2001-01-01T00:00:01Z';", NULL);
    sparql_query = g_strconcat (sparql_query, "\ntracker:available true", NULL);
    sparql_query = g_strconcat (sparql_query, ".\n}\n", NULL);

    g_printf ("%s", sparql_query);

    tracker_sparql_connection_update (connection,
                                      sparql_query,
                                      0,
                                      NULL,
                                      NULL);

    priv->providers_running = 0;
    priv->providers_finished = 0;
    priv->providers_error = 0;

    priv->restart = FALSE;
    priv->running++;

    nautilus_search_provider_start (NAUTILUS_SEARCH_PROVIDER (priv->tracker));

    g_main_loop_run (loop);
    return 0;
}
