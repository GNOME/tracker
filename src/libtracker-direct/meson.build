libtracker_direct = static_library('tracker-direct',
    'tracker-direct.c',
    c_args: tracker_c_args,
    dependencies: [ glib, gio, tracker_data_dep ],
    include_directories: [commoninc, configinc, srcinc],
)

tracker_sparql_direct_dep = declare_dependency(
    link_with: libtracker_direct,
    include_directories: include_directories('.')
)
