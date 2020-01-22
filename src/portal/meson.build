sources = [
    'tracker-main.c',
    'tracker-portal.c',
    'tracker-portal-utils.c',
]

executable('tracker-xdg-portal-@0@'.format(tracker_api_major), sources,
    c_args: tracker_c_args,
    install: true,
    install_rpath: tracker_install_rpath,
    install_dir: get_option('libexecdir'),
    dependencies: [tracker_sparql_dep],
    include_directories: [commoninc, configinc, srcinc],
)
