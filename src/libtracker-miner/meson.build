miner_enums = gnome.mkenums('tracker-miner-enum-types',
    sources: 'tracker-miner-enums.h',
    c_template: 'tracker-miner-enum-types.c.template',
    h_template: 'tracker-miner-enum-types.h.template',
    install_header: true,
    install_dir: join_paths(get_option('includedir'), 'tracker-@0@'.format(tracker_api_version), 'libtracker-miner'),
)

private_sources = [
    'tracker-crawler.c',
    'tracker-file-data-provider.c',
    'tracker-file-notifier.c',
    'tracker-file-system.c',
    'tracker-monitor.c',
    'tracker-priority-queue.c',
    'tracker-task-pool.c',
    'tracker-sparql-buffer.c',
    'tracker-utils.c']

miner_headers = [
    'tracker-miner-online.h',
    'tracker-data-provider.h',
    'tracker-indexing-tree.h',
    'tracker-decorator-fs.h',
    'tracker-miner-fs.h',
    'tracker-miner-object.h',
    'tracker-miner-proxy.h',
    'tracker-decorator.h',
    'tracker-miner-enums.h',
    'tracker-miner.h',
]

miner_sources = (
    ['tracker-data-provider.c',
    'tracker-decorator.c',
    'tracker-decorator-fs.c',
    'tracker-indexing-tree.c',
    'tracker-miner-object.c',
    'tracker-miner-online.c',
    'tracker-miner-proxy.c',
    'tracker-miner-fs.c'])

libtracker_miner_private = static_library(
    'tracker-miner-private',
    miner_enums[0], miner_enums[1], private_sources,
    dependencies: [tracker_common_dep, tracker_sparql_dep],
    c_args: tracker_c_args,
)

tracker_miner_dependencies = []
if have_network_manager
    tracker_miner_dependencies += network_manager
endif

mapfile = 'tracker-miner-2.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)

libtracker_miner = library(
    'tracker-miner-' + tracker_api_version,
    miner_enums[0], miner_enums[1], miner_sources,
    c_args: tracker_c_args,
    soversion: soversion,
    version: libversion,
    install: true,
    install_rpath: tracker_internal_libs_dir,
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    include_directories: [commoninc, configinc, srcinc],
    dependencies: [tracker_sparql_dep] + tracker_miner_dependencies,
    link_args: vflag,
    link_with: [libtracker_miner_private],
)

minerinc = include_directories('.')

tracker_miner_dep = declare_dependency(
    sources: miner_enums[1],
    link_with: libtracker_miner,
    include_directories: include_directories('.')
)

tracker_miner_gir = gnome.generate_gir(libtracker_miner,
    sources: miner_sources + miner_headers,
    nsversion: tracker_api_version,
    namespace: 'TrackerMiner',
    identifier_prefix: 'Tracker',
    symbol_prefix: 'tracker',
    # FIXME: also depends on Tracker-2.0.gir (output of libtracker-sparql)
    # but we can't currently access that from the Vala target
    includes : ['GLib-2.0', 'GObject-2.0', 'Gio-2.0' ],
    install: true,
    extra_args: tracker_c_args + [
        '--c-include=libtracker-miner/tracker-miner.h',
    ])

gnome.generate_vapi(
      'tracker-miner-' + tracker_api_version,
      sources : tracker_miner_gir[0],
      packages : 'gio-2.0',
      install : true,
    )

configure_file(
    input: 'tracker-miner.pc.in',
    output: 'tracker-miner-@0@.pc'.format(tracker_api_version),
    configuration: conf,
    install: true,
    install_dir: join_paths(get_option('prefix'), get_option('libdir'), 'pkgconfig'))

install_headers(miner_headers, subdir: 'tracker-@0@/libtracker-miner'.format(tracker_api_version))

install_data(
    'tracker-miner.xml',
    install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'tracker'))
