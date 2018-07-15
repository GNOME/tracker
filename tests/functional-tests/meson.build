test_runner = find_program('test-runner.sh')

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
  '12-transactions',
  '14-signals',
  '15-statistics',
  '16-collation',
  '17-ontology-changes',
  '200-backup-restore',
]

subdir('ttl')
functional_tests_with_test_data = [
  '10-sqlite-misused',
  '11-sqlite-batch-misused',
  '13-threaded-store',
]

config_json_full_path = join_paths(meson.current_build_dir(), 'configuration.json')
dconf_profile_full_path = join_paths(meson.current_source_dir(), 'trackertest')
tracker_nepomuk_ontologies = join_paths(meson.current_source_dir(), '..', '..', 'src', 'ontologies', 'nepomuk')
tracker_stop_words_dir = join_paths(meson.current_source_dir(), '..', '..', 'src', 'libtracker-common', 'stop-words')
tracker_test_domain_ontology = join_paths(meson.current_source_dir(), '..', '..', 'src', 'tracker-store', 'default.rule')

test_env = environment()
test_env.set('DCONF_PROFILE', dconf_profile_full_path)

test_env.set('TRACKER_DB_ONTOLOGIES_DIR', tracker_nepomuk_ontologies)
test_env.set('TRACKER_FUNCTIONAL_TEST_BUILD_DIR', build_root)
test_env.set('TRACKER_FUNCTIONAL_TEST_CONFIG', config_json_full_path)
test_env.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', tracker_stop_words_dir)
test_env.set('TRACKER_TEST_DOMAIN_ONTOLOGY_RULE', tracker_test_domain_ontology)

foreach t: functional_tests + functional_tests_with_test_data
  test('functional-' + t, test_runner,
    args: './' + t + '.py',
    env: test_env,
    workdir: meson.current_source_dir(),
    # FIXME: these tests are all too slow
    timeout: 180)
endforeach

subdir('ipc')
