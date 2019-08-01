libtracker_miner_tests = [
    'crawler',
    'file-enumerator',
    'file-system',
    'indexing-tree',
    'priority-queue',
    'task-pool',
    'thumbnailer',
]

libtracker_miner_slow_tests = [
    'file-notifier',
    'miner-fs',
    'monitor',
]

libtracker_miner_test_c_args = [
  '-DLIBEXEC_PATH="@0@/@1@"'.format(get_option('prefix'), get_option('libexecdir')),
  '-DTEST',
  '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir()),
  '-DTEST_MINERS_DIR="@0@/mock-miners"'.format(meson.current_source_dir()),
  '-DTEST_ONTOLOGIES_DIR="@0@/src/ontologies/nepomuk"'.format(source_root),
]

libtracker_miner_test_deps = [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep]

foreach base_name: libtracker_miner_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'miner-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_miner_test_deps,
      c_args: libtracker_miner_test_c_args,
      link_with: [libtracker_miner_private])

    tests += {
      'name': test_name,
      'exe': binary
    }
endforeach

foreach base_name: libtracker_miner_slow_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'miner-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_miner_test_deps,
      c_args: libtracker_miner_test_c_args,
      link_with: [libtracker_miner_private])

    tests += {
      'name': test_name,
      'exe': binary,
      'timeout': 180
    }
endforeach
