version_xml = configure_file(input: 'version.xml.in',
    output: 'version.xml',
    configuration: conf)

generated = custom_target('ontology-doc-generated',
    output: 'gen-doc.stamp',
    command: [ttl2xml,
              '-d', join_paths(source_root, 'src/ontologies/nepomuk'),
              '-o', join_paths(meson.current_build_dir(), 'xml/'),
              '-e', meson.current_source_dir()],
    depends: ttl2xml,
    depend_files: nepomuk,
    build_by_default: true,
)

gnome.gtkdoc('ontology',
    src_dir: include_directories('.'),
    main_xml: 'ontology-docs.xml',
    html_assets: icon_images,
    fixxref_args: fixxref_args,
    module_version: tracker_api_major,
    install: true)
