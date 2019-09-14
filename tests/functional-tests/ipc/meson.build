functional_ipc_test_c_args = [
  '-DTEST_DOMAIN_ONTOLOGY_RULE="@0@"'.format(tracker_uninstalled_domain_rule),
  '-DTEST_ONTOLOGIES_DIR="@0@"'.format(tracker_uninstalled_nepomuk_ontologies_dir),
]

sandbox_args = ['-m', 'trackertestutils', '--dbus-config', test_dbus_config, '--debug-sandbox', '--index-tmpdir', '--']

sandbox_env = environment()

test_env.prepend('PYTHONPATH', tracker_uninstalled_testutils_dir)
test_env.set('TRACKER_DB_ONTOLOGIES_DIR', tracker_uninstalled_nepomuk_ontologies_dir)
test_env.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', tracker_uninstalled_stop_words_dir)
test_env.set('TRACKER_TEST_DOMAIN_ONTOLOGY_RULE', tracker_uninstalled_domain_rule)

insert_or_replace_test = executable('test-insert-or-replace',
  'test-insert-or-replace.vala', tracker_sparql_vapi,
  dependencies: [tracker_common_dep, tracker_sparql_dep])
test('functional-ipc-insert-or-replace', python,
  args: sandbox_args + [insert_or_replace_test],
  env: test_env)

bus_query_cancellation_test = executable('test-bus-query-cancellation',
  'test-bus-query-cancellation.c',
  c_args: functional_ipc_test_c_args,
  dependencies: [tracker_common_dep, tracker_sparql_dep])

test('functional-ipc-bus-query-cancellation', python,
  args: sandbox_args + [bus_query_cancellation_test],
  env: test_env)
