tracker_store_sources = [
    'tracker-backup.vala',
    'tracker-config.c',
    'tracker-dbus.vala',
    'tracker-events.c',
    'tracker-main.vala',
    'tracker-resources.vala',
    'tracker-statistics.vala',
    'tracker-status.vala',
    'tracker-steroids.vala',
    'tracker-store.vala',
    'tracker-writeback.c',
    'tracker-config.vapi',
    'tracker-events.vapi',
    'tracker-locale-change.vapi',
    'tracker-writeback.vapi',
    '../libtracker-common/libtracker-common.vapi',
    '../libtracker-data/libtracker-data.vapi',
    '../libtracker-direct/tracker-direct.vapi',
]

tracker_store = executable('tracker-store',
    tracker_store_sources,
    c_args: tracker_c_args + [
        '-include', 'config.h'
    ],
    vala_args: [ '--pkg', 'posix' ],
    dependencies: [
        tracker_common_dep, tracker_sparql_dep, tracker_data_dep, tracker_sparql_direct_dep, gio_unix
    ],
    install: true,
    install_dir: join_paths(get_option('prefix'), get_option('libexecdir')),
    install_rpath: tracker_internal_libs_dir)

install_data(
    'tracker-backup.xml',
    'tracker-resources.xml',
    'tracker-statistics.xml',
    'tracker-status.xml',
    install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'tracker'))

dbus_service_file = configure_file(
    input: 'org.freedesktop.Tracker1.service.in',
    output: 'org.freedesktop.Tracker1.service',
    configuration: conf,
    install: true,
    install_dir: dbus_services_dir)

i18n.merge_file('tracker-store-desktop-file',
    type: 'desktop',
    input: 'tracker-store.desktop.in',
    output: 'tracker-store.desktop',
    po_dir: po_subdir,
    install: true,
    install_dir: join_paths(get_option('sysconfdir'), 'xdg/autostart'))

if install_systemd_user_services
  configure_file(
      input: 'tracker-store.service.in',
      output: 'tracker-store.service',
      configuration: conf,
      install: true,
      install_dir: systemd_user_services_dir)
endif

install_data('default.rule',
    install_dir: join_paths(get_option('datadir'), 'tracker', 'domain-ontologies'))
