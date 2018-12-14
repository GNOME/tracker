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
  test_name = t[0]
  test_exe = t[1]
  test_timeout = t.length() > 2 ? t[2] : 30

  test_env = environment()
  test_env.set('GSETTINGS_SCHEMA_DIR', join_paths(meson.current_build_dir(), '..', 'data'))

  test (test_name, dbus_run_session,
    env: test_env,
    args: ['--config-file=@0@'.format(join_paths(meson.current_build_dir(), 'test-bus.conf')),
           '--',
           test_exe],
    timeout: test_timeout)
endforeach
