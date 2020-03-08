#include <libtracker-sparql/tracker-sparql.h>

int main (int argc, const char **argv)
{
  GError *error = NULL;
  TrackerSparqlConnection *connection;
  const gchar *query =
    "INSERT { "
    "  _:tag a nao:Tag ; "
    "        nao:prefLabel 'mylabel' . "
    "} WHERE { "
    "  OPTIONAL { "
    "    ?tag a nao:Tag ; "
    "    nao:prefLabel 'mylabel' "
    "  } . "
    "FILTER (!bound(?tag)) "
    "}";

  connection = tracker_sparql_connection_bus_new ("org.freedesktop.Tracker3.Miner.Files", NULL, NULL, &error);
  if (!connection) {
    g_printerr ("Couldn't obtain a connection to the Tracker store: %s",
                error ? error->message : "unknown error");
    g_clear_error (&error);

    return 1;
  }

  /* Run a synchronous update query */
  tracker_sparql_connection_update (connection,
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

  g_object_unref (connection);

  return 0;
}
