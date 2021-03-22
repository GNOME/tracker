tests = []

test_bus_conf_file = configure_file(
  input: 'test-bus.conf.in',
  output: 'test-bus.conf',
  configuration: conf)

subdir('common')

subdir('gvdb')
subdir('libtracker-common')
subdir('libtracker-data')
subdir('libtracker-fts')
subdir('libtracker-sparql')
subdir('functional-tests')
subdir('services')

if get_option('tap_tests')
  protocol = 'tap'
else
  protocol = 'exitcode'
endif

foreach t: tests
  test_name = t.get('name')
  test_exe = t.get('exe')
  test_suite = t.get('suite', [])
  test_timeout = t.get('timeout', 30)

  # This flag marks unit tests which can't run alongside other unit tests. It
  # generally indicates a bug or design flaw in that test.
  test_is_parallel = t.get('is_parallel', true)

  test_env = environment()
  test_env.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', join_paths(source_root, 'src', 'libtracker-common', 'stop-words'))
  test_env.set('LANG', 'en_US.UTF8')

  test(test_name, test_exe,
    env: test_env,
    timeout: test_timeout,
    protocol: protocol,
    suite: test_suite,
    is_parallel: test_is_parallel)
endforeach
