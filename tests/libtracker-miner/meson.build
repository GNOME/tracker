test_c_args = [
  '-DLIBEXEC_PATH="@0@/@1@"'.format(get_option('prefix'), get_option('libexecdir')),
  '-DTEST',
  '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir()),
  '-DTEST_MINERS_DIR="@0@/mock-miners"'.format(meson.current_source_dir()),
  '-DTEST_ONTOLOGIES_DIR="@0@/src/ontologies/nepomuk"'.format(meson.source_root()),
]

environment = environment()
environment.set('TRACKER_LANGUAGE_STOP_WORDS_DIR', '@0@/src/libtracker-common/stop-words'.format(meson.source_root()))

crawler_test = executable('tracker-crawler-test',
  'tracker-crawler-test.c',
  shared_libtracker_miner_crawler_sources,
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-crawler', crawler_test)

file_enumerator_test = executable('tracker-file-enumerator-test',
  'tracker-file-enumerator-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-file-enumerator', file_enumerator_test,
     env: environment)

file_notifier_test = executable('tracker-file-notifier-test',
  'tracker-file-notifier-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-file-notifier', file_notifier_test,
     env: environment)

file_system_test = executable('tracker-file-system-test',
  'tracker-file-system-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-file-system', file_system_test)

indexing_tree_test = executable('tracker-indexing-tree-test',
  'tracker-indexing-tree-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-indexing-tree', indexing_tree_test)

miner_fs_test = executable('tracker-miner-fs-test',
  'tracker-miner-fs-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-fs', miner_fs_test,
  timeout: 180)

monitor_test = executable('tracker-monitor-test',
  'tracker-monitor-test.c',
  shared_libtracker_miner_monitor_sources,
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-monitor', monitor_test,
  # FIXME: why is this test so slow?
  timeout: 180)

priority_queue_test = executable('tracker-priority-queue-test',
  'tracker-priority-queue-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-priority-queue', priority_queue_test)

task_pool_test = executable('tracker-task-pool-test',
  'tracker-task-pool-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-task-pool', task_pool_test)

thumbnailer_test = executable('tracker-thumbnailer-test',
  'empty-gobject.c',
  'thumbnailer-mock.c',
  'tracker-thumbnailer-test.c',
  dependencies: [tracker_common_dep, tracker_miner_dep, tracker_sparql_dep],
  c_args: test_c_args
)
test('miner-thumbnailer', thumbnailer_test)
