sources = [
    'tracker-main.c',
    'tracker-portal.c',
    'tracker-portal-endpoint.c',
    'tracker-portal-utils.c',
]

executable('xdg-tracker-portal', sources,
    c_args: tracker_c_args,
    install: true,
    install_rpath: tracker_install_rpath,
    install_dir: get_option('libexecdir'),
    dependencies: [tracker_sparql_dep],
    include_directories: [commoninc, configinc, srcinc],
)

dbus_service_file = configure_file(
    input: 'org.freedesktop.portal.Tracker.service.in',
    output: 'org.freedesktop.portal.Tracker.service',
    configuration: conf,
    install: true,
    install_dir: dbus_services_dir,
)

if install_systemd_user_services
    configure_file(
        input: 'xdg-tracker-portal.service.in',
        output: 'xdg-tracker-portal.service',
        configuration: conf,
        install: true,
        install_dir: systemd_user_services_dir
    )
endif
