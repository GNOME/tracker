version_xml = configure_file(input: 'version.xml.in',
    output: 'version.xml',
    configuration: conf)

tracker_miner_fixxref_args = fixxref_args + [
    '--extra-dir=@0@'.format('../libtracker-sparql'),
]

gnome.gtkdoc('libtracker-miner',
    src_dir: minerinc,
    main_xml: 'libtracker-miner-docs.xml',
    content_files: ['overview.xml', 'migrating-1to2.xml'],
    dependencies: tracker_miner_dep,
    scan_args: ['--rebuild-sections'],
    fixxref_args: tracker_miner_fixxref_args,
    install: true)
