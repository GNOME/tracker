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
        tracker_common_dep, tracker_data_dep, tracker_sparql_direct_dep,
        gio_unix
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

dbus_service_file = custom_target('tracker-store-dbus-service-file',
    input: 'org.freedesktop.Tracker1.service.in',
    output: 'org.freedesktop.Tracker1.service',
    command: ['intltool-merge', '--quiet', '--desktop-style', '--utf8', po_subdir, '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: dbus_services_dir)

desktop_file_untranslated = configure_file(
    input: 'tracker-store.desktop.in.in',
    output: 'tracker-store.desktop.in',
    configuration: conf)

desktop_file = custom_target('tracker-store-desktop-file',
    input: desktop_file_untranslated,
    output: 'tracker-store.desktop',
    command: ['intltool-merge', '--quiet', '--desktop-style', '--utf8', po_subdir, '@INPUT@', '@OUTPUT@'],
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

custom_target('tracker-store-settings-schema',
    input: 'org.freedesktop.Tracker.Store.gschema.xml.in',
    output: 'org.freedesktop.Tracker.Store.gschema.xml',
    command: ['intltool-merge', '--quiet', '--xml-style', '--utf8', po_subdir, '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: gsettings_schema_dir)

custom_target('tracker-store-settings-schemalist',
    input: 'org.freedesktop.Tracker.gschema.xml.in',
    output: 'org.freedesktop.Tracker.gschema.xml',
    command: ['intltool-merge', '--quiet', '--xml-style', '--utf8', po_subdir, '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: gsettings_schema_dir)

custom_target('tracker-store-settings-enums',
    input: '../libtracker-common/tracker-enums.h',
    output: 'org.freedesktop.Tracker.enums.xml',
    command: [glib_mkenums,
              '--comments', '<!-- @comment@ -->',
              '--fhead', '<schemalist>',
              '--vhead', '<@type@ id="org.freedesktop.Tracker.@EnumName@">',
              '--vprod', '    <value nick="@valuenick@" value="@valuenum@"/>',
              '--vtail', '  </@type@>',
              '--ftail', '</schemalist>', '@INPUT@'],
    capture: true,
    install: true,
    install_dir: gsettings_schema_dir)

install_data('default.rule',
    install_dir: join_paths(get_option('datadir'), 'tracker', 'domain-ontologies'))
