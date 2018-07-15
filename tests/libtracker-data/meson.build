test_c_args = tracker_c_args + [
    '-DTOP_BUILDDIR="@0@"'.format(build_root),
    '-DTOP_SRCDIR="@0@"'.format(source_root),
]

backup_test = executable('tracker-backup-test',
    'tracker-backup-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-backup', backup_test)

crc32_test = executable('tracker-crc32-test',
    'tracker-crc32-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-crc32', crc32_test)

db_journal_test = executable('tracker-db-journal-test',
    'tracker-db-journal-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-db-journal', db_journal_test)

ontology_test = executable('tracker-ontology-test',
    'tracker-ontology-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-ontology', ontology_test,
    # FIXME: why is this test so slow?
    timeout: 180)

ontology_change_test = executable('tracker-ontology-change-test',
    'tracker-ontology-change-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-ontology-change', ontology_change_test)

sparql_test = executable('tracker-data-sparql-test',
    'tracker-sparql-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-sparql', sparql_test,
    # FIXME: why is this test so slow?
    timeout: 180)

sparql_blank_test = executable('tracker-sparql-blank-test',
    'tracker-sparql-blank-test.c',
    dependencies: [tracker_common_dep, tracker_data_dep],
    c_args: test_c_args)
test('data-sparql-blank', sparql_blank_test)
