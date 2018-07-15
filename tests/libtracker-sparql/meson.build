libtracker_sparql_tests = [
    'gb-737023',
    'resource',
    'sparql',
]

libtracker_sparql_test_c_args = tracker_c_args + [
  '-DTEST',
  '-DTEST_DOMAIN_ONTOLOGY_RULE="@0@/src/tracker-store/default.rule"'.format(source_root),
  '-DTEST_ONTOLOGIES_DIR="@0@/src/ontologies/nepomuk"'.format(source_root),
]

libtracker_sparql_test_deps = [
    tracker_common_dep, tracker_sparql_dep
]

foreach base_name: libtracker_sparql_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'sparql-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_sparql_test_deps,
      c_args: libtracker_sparql_test_c_args)

    test(test_name, binary)
endforeach
