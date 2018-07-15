libtracker_sparql_tests = [

l
test_c_args = [
  '-DTEST',
  '-DTEST_DOMAIN_ONTOLOGY_RULE="@0@/src/tracker-store/default.rule"'.format(source_root),
  '-DTEST_ONTOLOGIES_DIR="@0@/src/ontologies/nepomuk"'.format(source_root),
]

resource_test = executable('tracker-resource-test',
  'tracker-resource-test.c',
  dependencies: [tracker_common_dep, tracker_sparql_dep],
  c_args: tracker_c_args)
test('sparql-resource', resource_test)

sparql_test = executable('tracker-sparql-test',
  'tracker-sparql-test.c',
  dependencies: [tracker_common_dep, tracker_sparql_dep],
  c_args: [tracker_c_args, test_c_args])
test('sparql', sparql_test)

gb_737023_test = executable('tracker-gb-737023-test',
  'tracker-gb-737023-test.c',
  dependencies: [tracker_common_dep, tracker_sparql_dep],
  c_args: [tracker_c_args, test_c_args])
test('gb_737023', sparql_test)
