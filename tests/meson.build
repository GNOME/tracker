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
