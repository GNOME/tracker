mapfile = 'tracker-sparql-2.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)

libtracker_sparql = library('tracker-sparql-' + tracker_api_version,
    '../libtracker-common/libtracker-common.vapi',
    '../libtracker-data/libtracker-data.vapi',
    '../libtracker-direct/tracker-direct.vapi',
    'tracker-backend.vala',

    soversion: soversion,

    install: true,
    install_rpath: tracker_internal_libs_dir,

    link_args: vflag,
    link_whole: [libtracker_sparql_intermediate_c, libtracker_sparql_intermediate_vala],

    # We need to pass this because link_whole doesn't trigger the .vapi
    # to be included in some versions of Meson.
    # https://github.com/mesonbuild/meson/pull/2030
    link_with: libtracker_sparql_intermediate_vala,

    dependencies: [tracker_common_dep, tracker_sparql_remote_dep, tracker_sparql_bus_dep, tracker_sparql_direct_dep, libtracker_sparql_c_vapi_dep],
)

tracker_sparql_dep = declare_dependency(
    link_with: [libtracker_sparql, libtracker_sparql_intermediate_vala],
    include_directories: srcinc,
    dependencies: [tracker_common_dep, libtracker_sparql_c_vapi_dep],
)

# The introspection generation for libtracker-sparql is awkward because we have
# both C and Vala code, and we have to generate an introspection repo for each
# one separately and then combine them together manually.
#
# See: <https://bugzilla.gnome.org/show_bug.cgi?id=782091>

tracker_sparql_c_gir = gnome.generate_gir(libtracker_sparql,
    sources: libtracker_sparql_c_sources + libtracker_sparql_c_public_headers,
    nsversion: tracker_api_version,
    namespace: 'Tracker_C',
    identifier_prefix: 'Tracker',
    symbol_prefix: 'tracker',
    includes : ['GLib-2.0', 'GObject-2.0', 'Gio-2.0' ],
    link_with: libtracker_sparql,
    extra_args: [
        '--c-include', 'libtracker-sparql/tracker-sparql.h',
    ])

tracker_sparql_c_gir = tracker_sparql_c_gir[0]  # Ignore this .typelib
tracker_sparql_vala_gir = join_paths(meson.current_build_dir(), '..', 'libtracker-sparql', 'Tracker_Vala-' + tracker_api_version + '.gir')

tracker_sparql_gir = custom_target('tracker-sparql-gir',
    command: [g_ir_merge, '--namespace', 'Tracker', '--nsversion', tracker_api_version, '@INPUT@', tracker_sparql_vala_gir],
    input: [tracker_sparql_c_gir],
    capture: true,
    output: 'Tracker-@0@.gir'.format(tracker_api_version),
    install: true,
    install_dir: gir_dir)

tracker_sparql_typelib = custom_target('tracker-sparql-typelib',
    command: [g_ir_compiler, '--output', '@OUTPUT@', '@INPUT@'],
    input: tracker_sparql_gir,
    output: 'Tracker-@0@.typelib'.format(tracker_api_version),
    install: true,
    install_dir: typelib_dir)
