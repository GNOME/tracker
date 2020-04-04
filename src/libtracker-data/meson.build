tracker_data_enums = gnome.mkenums('tracker-data-enum-types',
    sources: 'tracker-db-interface-sqlite.h',
    c_template: 'tracker-data-enum-types.c.template',
    h_template: 'tracker-data-enum-types.h.template',
)

tracker_data_dependencies = [
    tracker_common_dep, gvdb_dep, sqlite,
]

tracker_data_dependencies += tracker_fts_dep

libtracker_data = library('tracker-data',
    'tracker-class.c',
    'tracker-collation.c',
    'tracker-crc32.c',
    'tracker-data-manager.c',
    'tracker-data-query.c',
    'tracker-data-update.c',
    'tracker-db-interface.c',
    'tracker-db-interface-sqlite.c',
    'tracker-db-manager.c',
    'tracker-namespace.c',
    'tracker-ontology.c',
    'tracker-ontologies.c',
    'tracker-property.c',
    'tracker-string-builder.c',
    'tracker-sparql-parser.c',
    'tracker-sparql-types.c',
    'tracker-sparql.c',
    'tracker-turtle-reader.c',
    'tracker-uuid.c',
    'tracker-vtab-service.c',
    'tracker-vtab-triples.c',
    tracker_common_enum_header,
    tracker_data_enums[0],
    tracker_data_enums[1],
    tracker_gresources,
    c_args: tracker_c_args,
    install: true,
    install_dir: tracker_internal_libs_dir,
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    # We do *not* want --no-undefined here, this library uses
    # symbols from libtracker-sparql, but does not mean it should
    # be linked to it.
    override_options: ['b_lundef=false'],
    dependencies: tracker_data_dependencies + [unicode_library],
    include_directories: [commoninc, configinc, srcinc],
)

tracker_data_dep = declare_dependency(
    link_with: [libtracker_data],
    dependencies: tracker_data_dependencies,
    include_directories: include_directories('.'),
    sources: enums[1],
)
