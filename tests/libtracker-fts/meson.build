fts_test = executable('tracker-fts-test',
  'tracker-fts-test.c',
  dependencies: [tracker_common_dep, tracker_sparql_dep, tracker_data_dep, tracker_testcommon_dep],
  c_args: test_c_args
)

tests += {
  'name': 'fts',
  'exe': fts_test,
  'requires_dbus': true,
}
