libtracker_fts_test_environment = environment()
libtracker_fts_test_environment.set('GSETTINGS_SCHEMA_DIR', join_paths(meson.build_root(), 'data'))

fts_test = executable('tracker-fts-test',
  'tracker-fts-test.c',
  dependencies: [tracker_common_dep, tracker_sparql_dep, tracker_data_dep, tracker_testcommon_dep],
  c_args: test_c_args
)

test('fts', fts_test,
  env: libtracker_fts_test_environment)
