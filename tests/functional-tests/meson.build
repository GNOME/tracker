python = find_program('python3')

testconf = configuration_data()

config_json_full_path = join_paths(meson.current_build_dir(), 'configuration.json')

testconf.set('TEST_CLI_DIR', tracker_uninstalled_cli_dir)
testconf.set('TEST_ONTOLOGIES_DIR', tracker_uninstalled_nepomuk_ontologies_dir)
testconf.set('TRACKER_VERSION', meson.project_version())

config_json = configure_file(
  input: 'configuration.json.in',
  output: 'configuration.json',
  configuration: testconf
)

functional_tests = [
  'insertion',
  'query',
  'sparql-bugs',
  'group-concat',
  'coalesce',
  'distance',
  'graph',
  'concurrent-query',
  'notifier',
  'collation',
  'ontology-changes',
  'cli',
]

if get_option('fts')
  functional_tests += 'fts-functions'
endif

test_env = environment()
test_env.prepend('GI_TYPELIB_PATH', tracker_sparql_uninstalled_dir)
test_env.prepend('LD_LIBRARY_PATH', tracker_sparql_uninstalled_dir)
test_env.prepend('PYTHONPATH', tracker_uninstalled_testutils_dir)
test_env.set('TRACKER_FUNCTIONAL_TEST_CONFIG', config_json_full_path)

foreach test_name: functional_tests
  file = meson.current_source_dir() / '@0@.py'.format(test_name)
  test(test_name, python,
    args: [file],
    env: test_env,
    suite: ['functional'],
    timeout: 60)
endforeach

subdir('ipc')
