#include <libtracker-sparql/tracker-sparql.h>

int main (int argc, char **argv)
{
  TrackerSparqlBuilder *builder;
  const gchar *iri = "urn:example:0001";
  const gchar *query_str;
  time_t now;

  /* Create builder */
  builder = tracker_sparql_builder_new_update ();

  /* Insert new data */
  tracker_sparql_builder_insert_open (builder, NULL);

  tracker_sparql_builder_subject_iri (builder, iri);

  tracker_sparql_builder_predicate (builder, "a");
  tracker_sparql_builder_object (builder, "nie:DataObject");
  tracker_sparql_builder_object (builder, "nfo:FileDataObject");

  now = time (NULL);
  tracker_sparql_builder_predicate (builder, "nfo:fileLastModified");
  tracker_sparql_builder_object_date (builder, &now);

  tracker_sparql_builder_insert_close (builder);

  /* Get query as string. Do NOT g_free() the resulting string! */
  query_str = tracker_sparql_builder_get_result (builder);

  /* Print it */
  g_print ("Generated SPARQL query: '%s'\n", query_str);

  /* Once builder no longer needed, unref it. Note that after
   * this operation, you must not use the returned query result
   * any more
   */
  g_object_unref (builder);

  return 0;
}
