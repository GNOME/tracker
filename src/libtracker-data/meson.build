# Vala parts of libtracker-data are split out separately, because we can't link
# the Vala code to libicu. In the Makefile.am this library is called
# libtracker-sparql-query.
libtracker_data_vala = static_library('tracker-sparql-query',
    'tracker-vala-namespace.vala',
    'tracker-sparql-scanner.vala',
    'tracker-turtle-reader.vala',
    '../libtracker-common/libtracker-common.vapi',
    'libtracker-data.vapi',
    tracker_sparql_vapi,
    tracker_common_enum_header,
    c_args: tracker_c_args,
    vala_args: [
        '--debug',
        '--pkg', 'posix',
        # FIXME: Meson has code to add --target-glib automatically, but it
        # doesn't seem to work here.
        '--target-glib', glib_required,
    ],
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    dependencies: [glib, gio],
    include_directories: [commoninc, configinc, srcinc],
)

tracker_data_enums = gnome.mkenums('tracker-data-enum-types',
    sources: 'tracker-db-interface-sqlite.h',
    c_template: 'tracker-data-enum-types.c.template',
    h_template: 'tracker-data-enum-types.h.template',
)

tracker_data_dependencies = [
    tracker_common_dep, gvdb_dep, sqlite, uuid,
]

if get_option('fts')
    tracker_data_dependencies += tracker_fts_dep
endif

libtracker_data = library('tracker-data',
    'tracker-class.c',
    'tracker-collation.c',
    'tracker-crc32.c',
    'tracker-data-backup.c',
    'tracker-data-manager.c',
    'tracker-data-query.c',
    'tracker-data-update.c',
    'tracker-db-config.c',
    'tracker-db-interface.c',
    'tracker-db-interface-sqlite.c',
    'tracker-db-manager.c',
    'tracker-db-journal.c',
    'tracker-db-backup.c',
    'tracker-namespace.c',
    'tracker-ontology.c',
    'tracker-ontologies.c',
    'tracker-property.c',
    'tracker-string-builder.c',
    'tracker-sparql-parser.c',
    'tracker-sparql-types.c',
    'tracker-sparql.c',
    'tracker-uuid.c',
    'tracker-vtab-triples.c',
    tracker_common_enum_header,
    tracker_data_enums[0],
    tracker_data_enums[1],
    tracker_gresources[0],
    tracker_gresources[1],
    c_args: tracker_c_args,
    install: true,
    install_dir: tracker_internal_libs_dir,
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    link_with: [libtracker_data_vala],
    # We do *not* want --no-undefined here, this library uses
    # symbols from libtracker-sparql, but does not mean it should
    # be linked to it.
    override_options: ['b_lundef=false'],
    dependencies: tracker_data_dependencies + [unicode_library, tracker_sparql_intermediate_dep],
    include_directories: [commoninc, configinc, srcinc],
)

tracker_data_dep = declare_dependency(
    link_with: [libtracker_data, libtracker_data_vala],
    dependencies: tracker_data_dependencies,
    include_directories: include_directories('.'),
    sources: enums[1],
)
