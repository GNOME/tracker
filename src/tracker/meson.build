modules = [
    'endpoint',
    'export',
    'help',
    'import',
    'info',
    'sparql',
    'sql',
]

sources = [
    'tracker-main.c',
]

foreach m: modules
    sources += 'tracker-@0@.c'.format(m)
endforeach

command_name = 'tracker@0@'.format(tracker_api_major)
subcommands_dir = join_paths(get_option('prefix'), get_option('libexecdir'), tracker_versioned_name)

executable(command_name, sources,
    c_args: tracker_c_args + [
        '-DCOMMANDNAME="@0@"'.format(command_name),
        '-DSUBCOMMANDSDIR="@0@"'.format(subcommands_dir),
        '-DMANDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'), 'man')),
        '-DBINDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('bindir'))),
    ],
    install: true,
    install_rpath: tracker_install_rpath,
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    dependencies: [tracker_sparql_dep, tracker_data_dep],
    include_directories: [commoninc, configinc, srcinc],
)

meson.add_install_script('make-subcommand-links.sh',
    [ '@0@'.format(join_paths(get_option('prefix'), get_option('bindir'), command_name)),
      '@0@'.format(subcommands_dir),
    ] + modules)

if get_option('bash_completion')
    install_data(
        sources: 'bash-completion/tracker',
        install_dir: bash_completion_dir,
	rename: command_name)
endif

run_command('make-uninstalled-subcommand-links.sh', modules,
            check: true)
