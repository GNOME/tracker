tracker_remote_dependencies = [json_glib, libsoup, libxml2]

sources = [
    'tracker-json-cursor.vala',
    'tracker-xml-cursor.vala',
    'tracker-remote.vala',
    '../libtracker-common/libtracker-common.vapi'
]

libtracker_remote = static_library('tracker-remote', sources,
    dependencies: tracker_remote_dependencies + [tracker_common_dep, tracker_sparql_intermediate_dep],
    c_args: tracker_c_args + [
        '-include', 'config.h'
    ],
    vala_args: [
        '--debug',
        '--pkg', 'posix',
        # FIXME: Meson has code to add --target-glib automatically, but it
        # doesn't seem to work here.
        '--target-glib', glib_required,
    ],
    gnu_symbol_visibility: 'hidden',
)

tracker_sparql_remote_dep = declare_dependency(
    link_with: libtracker_remote,
    include_directories: include_directories('.'),
    dependencies: tracker_remote_dependencies,
)
