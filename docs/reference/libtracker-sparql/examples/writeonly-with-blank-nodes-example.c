#include <libtracker-sparql/tracker-sparql.h>

int main (int argc, const char **argv)
{
  GError *error = NULL;
  GVariant *v;
  TrackerSparqlConnection *connection;
  const gchar *query =
    "INSERT { _:foo a nie:InformationElement } WHERE { ?x a rdfs:Class }";

  connection = tracker_sparql_connection_get (NULL, &error);
  if (!connection) {
    g_printerr ("Couldn't obtain a connection to the Tracker store: %s",
                error ? error->message : "unknown error");
    g_clear_error (&error);

    return 1;
  }

  /* Run a synchronous blank node update query */
  v = tracker_sparql_connection_update_blank (connection,
                                              query,
                                              G_PRIORITY_DEFAULT,
                                              NULL,
                                              &error);

  if (error) {
    /* Some error happened performing the query, not good */
    g_printerr ("Couldn't update the Tracker store: %s",
                error ? error->message : "unknown error");

    g_clear_error (&error);
    g_object_unref (connection);

    return 1;
  }

  if (!v) {
    g_print ("No results were returned\n");
  } else {
    GVariantIter iter1, *iter2, *iter3;
    const gchar *node;
    const gchar *urn;

    g_print ("Results:\n");

    g_variant_iter_init (&iter1, v);
    while (g_variant_iter_loop (&iter1, "aa{ss}", &iter2)) { /* aa{ss} */
      while (g_variant_iter_loop (iter2, "a{ss}", &iter3)) { /* a{ss} */
        while (g_variant_iter_loop (iter3, "{ss}", &node, &urn)) { /* {ss} */
          g_print ("  Node:'%s', URN:'%s'\n", node, urn);
        }
      }
    }

    g_variant_unref (v);
  }

  g_object_unref (connection);

  return 0;
}
