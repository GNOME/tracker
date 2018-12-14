libtracker_sparql_tests = [
    'resource',
    'sparql',
]

libtracker_sparql_test_c_args = tracker_c_args + [
  '-DTEST',
  '-DTEST_DOMAIN_ONTOLOGY_RULE="@0@"'.format(tracker_uninstalled_domain_rule),
  '-DTEST_ONTOLOGIES_DIR="@0@"'.format(tracker_uninstalled_nepomuk_ontologies_dir),
]

libtracker_sparql_test_deps = [
    tracker_common_dep, tracker_sparql_dep
]

libtracker_sparql_test_environment = environment()
libtracker_sparql_test_environment.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', '@0@/src/libtracker-common/stop-words'.format(source_root))
libtracker_sparql_test_environment.set('GSETTINGS_SCHEMA_DIR', join_paths(meson.build_root(), 'data'))

foreach base_name: libtracker_sparql_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'sparql-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_sparql_test_deps,
      c_args: libtracker_sparql_test_c_args)

    test(test_name, binary,
      env: libtracker_sparql_test_environment)
endforeach
