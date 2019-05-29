tests = []

subdir('common')

subdir('gvdb')
subdir('libtracker-common')
subdir('libtracker-data')

if enable_fts
  subdir('libtracker-fts')
endif

subdir('libtracker-miner')
subdir('libtracker-sparql')
subdir('tracker-steroids')

if get_option('functional_tests')
  subdir('functional-tests')
endif

subdir('services')

test_bus_conf_file = configure_file(
  input: 'test-bus.conf.in',
  output: 'test-bus.conf',
  configuration: conf)

foreach t: tests
  test_name = t.get('name')
  test_exe = t.get('exe')
  test_timeout = t.get('timeout', 30)

  # This flag marks unit tests which can't run alongside other unit tests. It
  # generally indicates a bug or design flaw in that test.
  test_is_parallel = t.get('is_parallel', true)

  # This flag marks unit tests which need to contact a D-Bus session bus. This
  # is a sign of a bad unit test -- anything which uses D-Bus belongs in the
  # functional-tests directory.
  test_requires_dbus = t.get('requires_dbus', false)

  test_env = environment()
  test_env.set('GSETTINGS_SCHEMA_DIR', join_paths(build_root, 'data'))
  test_env.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', join_paths(source_root, 'src', 'libtracker-common', 'stop-words'))
  test_env.set('TRACKER_TEST_DOMAIN_ONTOLOGY_RULE', join_paths(source_root, 'src', 'tracker-store', 'default.rule'))
  test_env.set('TRACKER_DB_ONTOLOGIES_DIR', join_paths(source_root, 'src', 'ontologies', 'nepomuk'))
  test_env.set('LANG', 'en_US.UTF8')

  if test_requires_dbus
    test (test_name, dbus_run_session,
      env: test_env,
      args: ['--config-file=@0@'.format(join_paths(meson.current_build_dir(), 'test-bus.conf')),
             '--',
             test_exe],
      timeout: test_timeout,
      is_parallel: test_is_parallel)
  else
    test (test_name, test_exe,
          env: test_env,
          timeout: test_timeout,
          is_parallel: test_is_parallel)
  endif
endforeach
