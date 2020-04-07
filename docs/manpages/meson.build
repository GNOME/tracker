manpages = [
    'endpoint',
    'export',
    'import',
    'info',
    'sparql',
    'sql'
]

foreach m: manpages
    manpage = configure_file(
        input: 'tracker-@0@.1'.format(m),
        output: 'tracker@0@-@1@.1'.format(tracker_api_major, m),
        copy: true,
        install: false)
    install_man(manpage)
endforeach
