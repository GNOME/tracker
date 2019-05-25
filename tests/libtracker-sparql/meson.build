libtracker_sparql_test_c_args = tracker_c_args + [
  '-DTEST',
  '-DTEST_DOMAIN_ONTOLOGY_RULE="@0@"'.format(tracker_uninstalled_domain_rule),
  '-DTEST_ONTOLOGIES_DIR="@0@"'.format(tracker_uninstalled_nepomuk_ontologies_dir),
]

libtracker_sparql_test_deps = [
    tracker_common_dep, tracker_sparql_dep
]

tracker_resource_test = executable('tracker-resource-test',
  'tracker-resource-test.c',
  dependencies: libtracker_sparql_test_deps,
  c_args: libtracker_sparql_test_c_args)

tests += {
  'name': 'tracker-resource-test',
  'exe': tracker_resource_test,
}

tracker_sparql_test = executable('tracker-sparql-test',
  'tracker-sparql-test.c',
  dependencies: libtracker_sparql_test_deps,
  c_args: libtracker_sparql_test_c_args)

tests += {
  'name': 'tracker-sparql-test',
  'exe': tracker_sparql_test,
  'is_parallel': false,
  'requires_dbus': true,
}
