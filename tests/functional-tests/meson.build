python = find_program('python3')

# Configure functional tests to run completely from source tree.
testconf = configuration_data()

config_json_full_path = join_paths(meson.current_build_dir(), 'configuration.json')
dconf_profile_full_path = join_paths(meson.current_source_dir(), 'trackertest')

test_dbus_config = join_paths(build_root, 'tests', 'test-bus.conf')

testconf.set('TEST_DBUS_DAEMON_CONFIG_FILE', test_dbus_config)
testconf.set('TEST_DCONF_PROFILE', dconf_profile_full_path)
testconf.set('TEST_DOMAIN_ONTOLOGY_RULE', tracker_uninstalled_domain_rule)
testconf.set('TEST_ONTOLOGIES_DIR', tracker_uninstalled_nepomuk_ontologies_dir)
testconf.set('TEST_LANGUAGE_STOP_WORDS_DIR', tracker_uninstalled_stop_words_dir)

config_json = configure_file(
  input: 'configuration.json.in',
  output: 'configuration.json',
  configuration: testconf
)

functional_tests = [
  '01-insertion',
  '02-sparql-bugs',
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

if get_option('fts')
  functional_tests += '03-fts-functions'
endif

test_env = environment()
test_env.prepend('PYTHONPATH', tracker_uninstalled_testutils_dir)
test_env.set('TRACKER_FUNCTIONAL_TEST_CONFIG', config_json_full_path)

foreach t: functional_tests
  file = '@0@.py'.format(t)
  test_parts = t.split('-')
  test_name = t
  if test_parts.length() > 1
    parts = []
    i = 0
    foreach p: test_parts
      if i > 0
        parts += p
      endif
      i += 1
    endforeach
    test_name = '-'.join(parts)
  endif
  test(test_name, python,
    args: [file],
    env: test_env,
    workdir: meson.current_source_dir(),
    suite: ['functional'],
    timeout: 60)
endforeach

subdir('ipc')
