mapfile = 'tracker-sparql-2.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)

libtracker_sparql = library('tracker-sparql-' + tracker_api_version,
    '../libtracker-common/libtracker-common.vapi',
    '../libtracker-data/libtracker-data.vapi',
    '../libtracker-direct/tracker-direct.vapi',
    'tracker-backend.vala',

    soversion: soversion,
    version: libversion,

    install: true,
    install_rpath: tracker_internal_libs_dir,

    link_args: vflag,
    link_whole: [libtracker_sparql_intermediate],

    dependencies: [tracker_common_dep, tracker_sparql_remote_dep, tracker_sparql_bus_dep, tracker_sparql_direct_dep, libtracker_sparql_c_vapi_dep],
)

tracker_sparql_dep = declare_dependency(
    link_with: libtracker_sparql,
    include_directories: srcinc,
    dependencies: [tracker_common_dep],
)

pkg.generate(libtracker_sparql,
    description: 'Tracker : A SPARQL triple store library',
    requires: [glib, gio, gobject, gmodule],
    subdirs: [
        'tracker-' + tracker_api_version,
        'tracker-' + tracker_api_version / 'libtracker-sparql',
    ],
    variables: [
        'exec_prefix=${prefix}',
        'libexecdir=${prefix}' / get_option('libexecdir'),
        'datadir=${prefix}' / get_option('datadir'),
        'tracker_datadir=${datadir}' / 'tracker',
        'ontologies_dir=' + tracker_ontologies_dir,
        'domain_ontologies_dir=${tracker_datadir}' / 'domain-ontologies',
        'dbus_services_dir=' + dbus_services_dir,
    ],
)

tracker_sparql_gir = gnome.generate_gir(libtracker_sparql,
    sources: libtracker_sparql_c_sources + libtracker_sparql_c_public_headers,
    nsversion: tracker_api_version,
    namespace: 'Tracker',
    identifier_prefix: 'Tracker',
    symbol_prefix: 'tracker',
    includes : ['GLib-2.0', 'GObject-2.0', 'Gio-2.0' ],
    link_with: libtracker_sparql,
    install: true,
    extra_args: [
        '--c-include',
        'libtracker-sparql/tracker-sparql.h',
        '-DTRACKER_COMPILATION',
    ])
