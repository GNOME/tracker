version_xml = configure_file(input: 'version.xml.in',
    output: 'version.xml',
    configuration: conf)

generated = custom_target('base-ontology-doc-generated',
    output: 'gen-doc.stamp',
    command: [ttl2xml,
              '-d', join_paths(source_root, 'src/ontologies/'),
              '-o', join_paths(meson.current_build_dir(), 'xml/'),
              '-e', meson.current_source_dir()],
    depends: ttl2xml,
    depend_files: base_ontology,
    build_by_default: true,
)

example_files = [
    'examples/ontologies/defining-cardinality-1.rq', 'examples/ontologies/defining-cardinality-2.txt',
    'examples/ontologies/defining-cardinality-3.rq', 'examples/ontologies/defining-classes-1.txt',
    'examples/ontologies/defining-classes-2.txt', 'examples/ontologies/defining-classes-3.rq',
    'examples/ontologies/defining-fts-indexes-1.txt', 'examples/ontologies/defining-fts-indexes-2.rq',
    'examples/ontologies/defining-indexes-1.txt', 'examples/ontologies/defining-namespaces-1.txt',
    'examples/ontologies/defining-properties-1.txt', 'examples/ontologies/defining-properties-2.txt',
    'examples/ontologies/defining-properties-3.txt', 'examples/ontologies/defining-properties-4.rq',
    'examples/ontologies/defining-uniqueness-1.txt', 'examples/ontologies/defining-uniqueness-2.rq',
    'examples/ontologies/example.description', 'examples/ontologies/predefined-elements-1.txt',
    'examples/ontologies/predefined-elements-2.rq',
    'examples/readonly-example.c', 'examples/writeonly-example.c',
    'examples/writeonly-with-blank-nodes-example.c',
]

private_headers = [
    'tracker-notifier-private.h',
]

gnome.gtkdoc('libtracker-sparql',
    src_dir: sparqlinc,
    main_xml: 'libtracker-sparql-docs.xml',
    scan_args: ['--ignore-headers=' + ' '.join(private_headers)],
    content_files: ['overview.xml', 'examples.xml', 'ontologies.xml', 'private-store.xml', 'migrating-1to2.xml', example_files],
    html_assets: icon_images,
    gobject_typesfile: 'libtracker-sparql.types',
    dependencies: tracker_sparql_dep,
    fixxref_args: fixxref_args,
    install_dir: 'libtracker-sparql@0@'.format(tracker_api_major),
    install: true)

subdir('examples')
