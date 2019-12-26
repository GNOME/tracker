sources = [
    'tracker-miner-manager.c',
    'tracker-main.c',
    'tracker-config.c',
    'tracker-daemon.c',
    'tracker-dbus.c',
    'tracker-extract.c',
    'tracker-help.c',
    'tracker-index.c',
    'tracker-info.c',
    'tracker-process.c',
    'tracker-reset.c',
    'tracker-search.c',
    'tracker-sparql.c',
    'tracker-sql.c',
    'tracker-status.c',
    'tracker-tag.c',
]

executable('tracker', sources,
    c_args: tracker_c_args + [
        '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
        '-DMANDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'), 'man')),
    ],
    install: true,
    install_rpath: tracker_install_rpath,
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    dependencies: [tracker_sparql_dep, tracker_data_dep],
    include_directories: [commoninc, configinc, srcinc],
)


if install_bash_completion
    install_data(
        sources: 'bash-completion/tracker',
        install_dir: bash_completion_dir)
endif
