sources = ['tracker-miner-manager.c']

headers = ['tracker-control.h', 'tracker-miner-manager.h']

mapfile = 'tracker-control-2.map'
vflag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)

libtracker_control = library('tracker-control-' + tracker_api_version,
    sources,
    tracker_common_enum_header,
    c_args: tracker_c_args,
    soversion: soversion,
    version: libversion,
    install: true,
    install_rpath: tracker_internal_libs_dir,
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    dependencies: [gio, tracker_sparql_dep, tracker_miner_dep],
    include_directories: [commoninc, configinc, srcinc],
    link_args: vflag,
    link_with: libtracker_common,
)

controlinc = include_directories('.')

tracker_control_dep = declare_dependency(
    link_with: libtracker_control,
    dependencies: tracker_miner_dep
)

tracker_control_gir = gnome.generate_gir(libtracker_control,
    sources: sources + headers,
    nsversion: tracker_api_version,
    namespace: 'TrackerControl',
    identifier_prefix: 'Tracker',
    symbol_prefix: 'tracker',
    includes : ['GLib-2.0', 'GObject-2.0', 'Gio-2.0' ],
    install: true,
    extra_args: '--c-include=libtracker-control/tracker-control.h')

gnome.generate_vapi(
      'tracker-control-' + tracker_api_version,
      sources : tracker_control_gir[0],
      packages : 'gio-2.0',
      install : true,
    )

configure_file(
    input: 'tracker-control.pc.in',
    output: 'tracker-control-@0@.pc'.format(tracker_api_version),
    configuration: conf,
    install: true,
    install_dir: join_paths(get_option('prefix'), get_option('libdir'), 'pkgconfig'))

install_headers (headers,
    subdir: 'tracker-@0@/libtracker-control'.format(tracker_api_version))
