#include <libtracker-sparql/tracker-sparql.h>

int main (int argc, const char **argv)
{
  GError *error = NULL;
  TrackerSparqlConnection *connection;
  TrackerSparqlCursor *cursor;
  const gchar *query = "SELECT nie:url(?u) WHERE { ?u a nfo:FileDataObject }";

  connection = tracker_sparql_connection_bus_new ("org.freedesktop.Tracker1", NULL, NULL, &error);
  if (!connection) {
    g_printerr ("Couldn't obtain a connection to the Tracker store: %s",
                error ? error->message : "unknown error");
    g_clear_error (&error);

    return 1;
  }

  /* Make a synchronous query to the store */
  cursor = tracker_sparql_connection_query (connection,
                                          query,
                                          NULL,
                                          &error);

  if (error) {
    /* Some error happened performing the query, not good */
    g_printerr ("Couldn't query the Tracker Store: '%s'",
                error ? error->message : "unknown error");
    g_clear_error (&error);

    return 1;
  }

  /* Check results... */
  if (!cursor) {
    g_print ("No results found :-/\n");
  } else {
    gint i = 0;

    /* Iterate, synchronously, the results... */
    while (tracker_sparql_cursor_next (cursor, NULL, &error)) {
      g_print ("Result [%d]: %s\n",
               i++,
               tracker_sparql_cursor_get_string (cursor, 0, NULL));
    }

    g_print ("A total of '%d' results were found\n", i);

    g_object_unref (cursor);
  }

  g_object_unref (connection);

  return 0;
}
