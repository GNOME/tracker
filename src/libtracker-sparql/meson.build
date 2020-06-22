enum_types = gnome.mkenums('tracker-sparql-enum-types',
    sources: ['tracker-notifier.h', 'tracker-connection.h'],
    c_template: 'tracker-sparql-enum-types.c.template',
    h_template: 'tracker-sparql-enum-types.h.template',
    install_dir: join_paths(get_option('prefix'), get_option('includedir'), 'tracker-@0@'.format(tracker_api_version), 'libtracker-sparql'),
    install_header: true,
)

tracker_sparql_vapi = files('tracker-sparql.vapi')
tracker_sparql_vapi_dep = meson.get_compiler('vala').find_library('tracker-sparql', dirs: meson.current_source_dir())

libtracker_sparql_c_sources = files(
    'tracker-connection.c',
    'tracker-cursor.c',
    'tracker-endpoint.c',
    'tracker-endpoint-dbus.c',
    'tracker-namespace-manager.c',
    'tracker-notifier.c',
    'tracker-resource.c',
    'tracker-statement.c',
    'tracker-uri.c',
    'tracker-utils.c',
    'tracker-version.c',
)

libtracker_sparql_c_public_headers = files(
    'tracker-connection.h',
    'tracker-cursor.h',
    'tracker-endpoint.h',
    'tracker-endpoint-dbus.h',
    'tracker-namespace-manager.h',
    'tracker-notifier.h',
    'tracker-ontologies.h',
    'tracker-resource.h',
    'tracker-statement.h',
    'tracker-utils.h',
    'tracker-version.h',
)

libtracker_sparql_intermediate = static_library('tracker-sparql-intermediate',
    enum_types,
    libtracker_sparql_c_sources,
    dependencies: [tracker_common_dep, json_glib],
    gnu_symbol_visibility: 'hidden',
)

sparqlinc = [include_directories('.'), meson.current_build_dir()]

tracker_sparql_intermediate_dep = declare_dependency(
    link_with: [libtracker_sparql_intermediate],
    include_directories: [srcinc, include_directories('.')],
    dependencies: [ tracker_sparql_vapi_dep ],
    sources: enum_types[1],
)

install_headers(
    libtracker_sparql_c_public_headers,
    'tracker-ontologies.h',
    'tracker-sparql.h',
    subdir: 'tracker-@0@/libtracker-sparql'.format(tracker_api_version))

install_data(
    tracker_sparql_vapi,
    rename: ['tracker-sparql-' + tracker_api_version + '.vapi'],
    install_dir: vapi_dir,
)

install_data(
    'tracker-sparql.deps',
    rename: ['tracker-sparql-' + tracker_api_version + '.deps'],
    install_dir: vapi_dir,
)

subdir('bus')
subdir('direct')
subdir('remote')

libtracker_sparql = library('tracker-sparql-' + tracker_api_version,
    '../libtracker-common/libtracker-common.vapi',
    '../libtracker-data/libtracker-data.vapi',
    'direct/tracker-direct.vapi',
    'tracker-backend.vala',

    gnu_symbol_visibility: 'hidden',

    soversion: soversion,
    version: libversion,

    install: true,
    install_rpath: tracker_internal_libs_dir,

    c_args: [
	'-include', 'libtracker-sparql/tracker-private.h',
    ],

    link_whole: [libtracker_sparql_intermediate],

    dependencies: [tracker_common_dep, tracker_sparql_remote_dep, tracker_sparql_bus_dep, tracker_sparql_direct_dep, tracker_sparql_vapi_dep],
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
        'tracker_datadir=${datadir}' / tracker_versioned_name,
        'ontologies_dir=' + tracker_ontologies_dir,
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

tracker_sparql_uninstalled_dir = meson.current_build_dir()
