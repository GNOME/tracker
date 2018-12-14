libtracker_common_tests = [
    'date-time',
    'dbus',
    'file-utils',
    'sched',
    'type-utils',
    'utils',
]

libtracker_common_test_deps = [
    tracker_common_dep, tracker_testcommon_dep
]

foreach base_name: libtracker_common_tests
    source = 'tracker-@0@-test.c'.format(base_name)
    binary_name = 'tracker-@0@-test'.format(base_name)
    test_name = 'common-@0@'.format(base_name)

    binary = executable(binary_name, source,
      dependencies: libtracker_common_test_deps,
      c_args: test_c_args)

    tests += {
        'name': test_name,
        'exe': binary
    }
endforeach

# This is a manual test case
parser_testcase = executable('tracker-parser',
    'tracker-parser.c',
    dependencies: tracker_common_dep,
    c_args: test_c_args,
)
