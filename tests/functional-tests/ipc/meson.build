functional_ipc_test_c_args = [
  '-DTEST_ONTOLOGIES_DIR="@0@"'.format(tracker_uninstalled_nepomuk_ontologies_dir),
]

test_env.set('LANG', 'en_GB.utf-8')
test_env.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', tracker_uninstalled_stop_words_dir)

bus_query_cancellation_test = executable('test-bus-query-cancellation',
  'test-bus-query-cancellation.c',
  c_args: functional_ipc_test_c_args,
  dependencies: [tracker_common_dep, tracker_sparql_dep])

test('bus-query-cancellation', bus_query_cancellation_test,
  env: test_env,
  suite: ['functional', 'ipc'],
  timeout: 60)
