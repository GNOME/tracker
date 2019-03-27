version_xml = configure_file(input: 'version.xml.in',
    output: 'version.xml',
    configuration: conf)

gnome.gtkdoc('libtracker-control',
    src_dir: controlinc,
    main_xml: 'libtracker-control-docs.xml',
    content_files: ['migrating-1to2.xml'],
    dependencies: tracker_control_dep,
    scan_args: ['--rebuild-sections'],
    fixxref_args: fixxref_args,
    install: true)
