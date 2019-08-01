test_runner = configure_file(
    input: 'test-runner.sh.in',
    output: 'test-runner.sh',
    configuration: conf)
test_runner = find_program(test_runner)

config_json = configure_file(
  input: 'configuration.json.in',
  output: 'configuration.json',
  configuration: conf
)

functional_tests = [
  '01-insertion',
  '02-sparql-bugs',
  '03-fts-functions',
  '04-group-concat',
  '05-coalesce',
  '06-distance',
  '07-graph',
  '08-unique-insertions',
  '09-concurrent-query',
  '14-signals',
  '15-statistics',
  '16-collation',
  '17-ontology-changes',
]

subdir('ttl')
functional_tests_with_test_data = [
  '13-threaded-store',
]

# These tests are disabled by default as they are really slow.
# '10-sqlite-misused',
# '11-sqlite-batch-misused',

config_json_full_path = join_paths(meson.current_build_dir(), 'configuration.json')
dconf_profile_full_path = join_paths(meson.current_source_dir(), 'trackertest')

test_env = environment()
test_env.set('DCONF_PROFILE', dconf_profile_full_path)
test_env.set('GSETTINGS_SCHEMA_DIR', tracker_uninstalled_gsettings_schema_dir)

test_env.set('TRACKER_DB_ONTOLOGIES_DIR', tracker_uninstalled_nepomuk_ontologies_dir)
test_env.set('TRACKER_FUNCTIONAL_TEST_BUILD_DIR', build_root)
test_env.set('TRACKER_FUNCTIONAL_TEST_CONFIG', config_json_full_path)
test_env.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', tracker_uninstalled_stop_words_dir)
test_env.set('TRACKER_TEST_DOMAIN_ONTOLOGY_RULE', tracker_uninstalled_domain_rule)

foreach t: functional_tests + functional_tests_with_test_data
  test('functional-' + t, test_runner,
    args: './' + t + '.py',
    env: test_env,
    workdir: meson.current_source_dir(),
    timeout: 60)
endforeach

subdir('ipc')
