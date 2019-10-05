#include <libtracker-sparql/tracker-sparql.h>

int main (int argc, char **argv)
{
  TrackerSparqlConnection *connection;
  TrackerSparqlStatement *statement;
  TrackerSparqlCursor *cursor;
  GError *error = NULL;
  GDateTime *new_datetime;
  gchar *formated_datetime;
  const gchar *iri = "<urn:example:0001>";
  const gchar *query_str =
    "DROP GRAPH ~iri\n"
    "INSERT INTO ~iri {\n"
    "~iri a nie:DataObject , nfo:FileDataObject ;\n"
    "       nfo:fileLastModified \"~time\"\n"
    "}";

  /* Get the SparqlConnection */
  connection = tracker_sparql_connection_get (NULL, &error);
  if (error) {
    g_critical ("Error getting a SPARQL connection: %s", error->message);
    g_clear_error (&error);
    return -1;
  }

  /* Create a new Statement */
  statement = tracker_sparql_connection_query_statement (connection,
                                                         query_str,
                                                         NULL,
                                                         &error);
  if (error) {
    g_critical ("Error querying the statement: %s", error->message);
    g_clear_error (&error);
    g_object_unref (connection);
    return -1;
  }

  /* Repace all the ~iri occurences with the actual iri */
  tracker_sparql_statement_bind_string (statement, "iri", iri);

  /* Repace all the ~time occurences with the current time */
  new_datetime = g_date_time_new_now_local ();
  formated_datetime = g_date_time_format (new_datetime, "%Y-%m-%dT%H:%M:%S%z");
  g_date_time_unref (new_datetime);
  tracker_sparql_statement_bind_string (statement, "time", formated_datetime);
  g_free (formated_datetime);

  cursor = tracker_sparql_statement_execute (statement, NULL, &error);
  if (error) {
    const gchar *sparql_query = tracker_sparql_statement_get_sparql (statement);
    g_critical ("Error executing the statement: %s\n[Query]\n%s", error->message, sparql_query);
    g_clear_error (&error);
    g_object_unref (statement);
    g_object_unref (connection);
    return -1;
  }

  g_object_unref (cursor);
  g_object_unref (statement);
  g_object_unref (connection);
  return 0;
}
