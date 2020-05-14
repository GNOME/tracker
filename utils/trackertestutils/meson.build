sources = [
  '__init__.py',
  '__main__.py',
  'dbusdaemon.py',
  'dconf.py',
  'helpers.py',
  'mainloop.py',
  'psutil_mini.py',
]

if get_option('test_utils')
  testutils_dir = get_option('test_utils_dir')

  if testutils_dir == ''
    arch_independent_libdir = \
      get_option('prefix') / 'lib' / 'tracker-' + tracker_api_version
    testutils_dir = join_paths(arch_independent_libdir)
  endif

  install_data(sources, install_dir: testutils_dir / 'trackertestutils')

  script_conf = configuration_data()
  script_conf.set('typelibdir', typelib_dir)
  script_conf.set('prefix', get_option('prefix'))
  script_conf.set('testutils_dir', testutils_dir)
  configure_file(
    input: 'tracker-sandbox.in',
    output: 'tracker-sandbox',
    configuration: script_conf,
    install_dir: testutils_dir / 'trackertestutils')

  pkg.generate(
    name: 'tracker-testutils-' + tracker_api_version,
    description: 'tracker test utilities',
    variables: [
      'python_path=' + testutils_dir,
      'command=' + testutils_dir / 'trackertestutils' / 'tracker-sandbox',
    ]
  )
endif
