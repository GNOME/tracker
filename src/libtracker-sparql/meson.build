enums_c = gnome.mkenums('tracker-sparql-enum-types',
    sources: 'tracker-notifier.h',
    c_template: 'tracker-sparql-enum-types.c.template',
)
enums_h = gnome.mkenums('tracker-sparql-enum-types',
    sources: 'tracker-notifier.h',
    h_template: 'tracker-sparql-enum-types.h.template',
)

tracker_sparql_intermediate_dependencies = [uuid]

# First build the Vala parts ...
libtracker_sparql_c_vapi = files('libtracker-sparql-intermediate-c.vapi')
libtracker_sparql_c_vapi_dep = meson.get_compiler('vala').find_library('libtracker-sparql-intermediate-c', dirs: meson.current_source_dir())

libtracker_sparql_intermediate_vala = static_library('tracker-sparql-intermediate-vala',
    'tracker-namespace.vala',
    'tracker-builder.vala',
    'tracker-connection.vala',
    'tracker-cursor.vala',
    'tracker-utils.vala',
    vala_header: 'tracker-generated-no-checks.h',
    c_args: tracker_c_args,
    vala_gir: 'Tracker_Vala-' + tracker_api_version + '.gir',
    vala_args: [
        '--debug',
        '--includedir', 'libtracker-sparql',
        '--pkg', 'gio-2.0', '--pkg', 'posix',
        # FIXME: Meson has code to add --target-glib automatically, but it
        # doesn't seem to work here.
        '--target-glib', glib_required,
        libtracker_sparql_c_vapi
    ],
    # This doesn't depend on tracker_common_dep because of
    # https://github.com/mesonbuild/meson/issues/671
    dependencies: [glib, gio] + tracker_sparql_intermediate_dependencies,
    include_directories: [commoninc, configinc, srcinc],
    link_with: [libtracker_common],
)

# We must postprocess the generated Vala header to have only-include-once guards
tracker_sparql_generated_header = custom_target('tracker-sparql-generated-header',
    # FIXME: can't list tracker-generated-no-checks as an input, even though it
    # is, because you'll see this:
    #
    #     ninja: error:
    #     '../src/libtracker-sparql/tracker-generated-no-checks.h', needed by
    #     'src/libtracker-sparql/tracker-generated.h', missing and no known
    #     rule to make it
    #
    output: 'tracker-generated.h',
    command: [join_paths(meson.current_source_dir(), 'tracker-sparql-add-include-guards.sh'), join_paths(meson.current_build_dir(), 'tracker-generated-no-checks.h'), '@OUTPUT@'],
    depends: libtracker_sparql_intermediate_vala,
    install: true,
    install_dir: join_paths(get_option('prefix'), get_option('includedir'), 'tracker-@0@'.format(tracker_api_version), 'libtracker-sparql'))

# Now build the C parts ...

libtracker_sparql_c_sources = files(
    'tracker-namespace-manager.c',
    'tracker-notifier.c',
    'tracker-resource.c',
    'tracker-uri.c',
    'tracker-version.c',
)

libtracker_sparql_c_public_headers = files(
    'tracker-namespace-manager.h',
    'tracker-notifier.h',
    'tracker-resource.h',
    'tracker-version.h',
)

libtracker_sparql_intermediate_c = static_library('tracker-sparql-intermediate-c',
    enums_c, enums_h,
    libtracker_sparql_c_sources,
    dependencies: [tracker_common_dep, json_glib],
)

tracker_sparql_intermediate_dep = declare_dependency(
    link_with: [libtracker_sparql_intermediate_c, libtracker_sparql_intermediate_vala],
    sources: tracker_sparql_generated_header,
    include_directories: [srcinc, include_directories('.')],
    dependencies: [ tracker_sparql_intermediate_dependencies, libtracker_sparql_c_vapi_dep ],
)

configure_file(
    input: 'tracker-sparql.pc.in',
    output: 'tracker-sparql-@0@.pc'.format(tracker_api_version),
    configuration: conf,
    install: true,
    install_dir: join_paths(get_option('prefix'), get_option('libdir'), 'pkgconfig'))

install_headers(
    libtracker_sparql_c_public_headers,
    'tracker-ontologies.h',
    'tracker-sparql.h',
    subdir: 'tracker-@0@/libtracker-sparql'.format(tracker_api_version))

# FIXME: we can't depend properly on the generated .vapi to do this.
custom_target('tracker-sparql-' + tracker_api_version + '.vapi',
    #input: 'tracker-sparql-intermediate-vala.vapi',
    output: 'tracker-sparql-' + tracker_api_version + '.vapi',
    command: [
        'sed', '-e', 's#libtracker-sparql/.*[.]h#libtracker-sparql/tracker-sparql.h#',
        libtracker_sparql_c_vapi,
        join_paths(meson.current_build_dir(), 'tracker-sparql-intermediate-vala.vapi')
    ],
    depends: libtracker_sparql_intermediate_vala,
    capture: true,
    install: true,
    install_dir: vapi_dir)

configure_file(
    input: 'tracker-sparql.deps',
    output: 'tracker-sparql-' + tracker_api_version + '.deps',
    configuration: configuration_data(), # No changes here, just a rename
    install: true,
    install_dir: vapi_dir)
