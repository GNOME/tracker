gvdb_test = executable('gvdb-test',
    'gvdb-test.c',
    dependencies: gvdb_dep,
    include_directories: configinc,
)

tests += {
  'name': 'gvdb',
  'exe': gvdb_test,
  'suite': ['gvdb']
}
