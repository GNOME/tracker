libtracker_bus = static_library('tracker-bus',
    'tracker-bus.vala',
    'tracker-namespace.vala',
    'tracker-bus-fd-cursor.vala',
    '../libtracker-common/libtracker-common.vapi',
    tracker_common_enum_header,
    c_args: tracker_c_args,
    vala_args: [
        '--debug',
        '--pkg', 'posix',
        # FIXME: Meson has code to add --target-glib automatically, but it
        # doesn't seem to work here.
        '--target-glib', glib_required,
    ],
    dependencies: [tracker_common_dep, tracker_sparql_intermediate_dep],
    include_directories: [commoninc, configinc, srcinc],
)

tracker_sparql_bus_dep = declare_dependency(
    link_with: libtracker_bus,
    include_directories: include_directories('.')
)
