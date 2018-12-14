libtracker_data_tests = [
    'backup',
    'crc32',
    'db-journal',
    'ontology-change',
    'sparql-blank',
]

libtracker_data_slow_tests = [
    'ontology',
    'sparql'
]

libtracker_data_test_environment = environment()
libtracker_data_test_environment.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', '@0@/src/libtracker-common/stop-words'.format(source_root))
libtracker_data_test_environment.set('GSETTINGS_SCHEMA_DIR', join_paths(meson.build_root(), 'data'))

libtracker_data_test_deps = [tracker_common_dep, tracker_data_dep, tracker_sparql_dep]

foreach base_name: libtracker_data_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'data-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_data_test_deps,
      c_args: test_c_args)

    test(test_name, binary,
      env: libtracker_data_test_environment)
endforeach

foreach base_name: libtracker_data_slow_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'data-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_data_test_deps,
      c_args: test_c_args)

    test(test_name, binary,
      timeout: 180,
      env: libtracker_data_test_environment)
endforeach
