tracker_gsettings_schemas = files([
  'org.freedesktop.Tracker.FTS.gschema.xml',
  'org.freedesktop.Tracker.gschema.xml',
])

foreach schema : tracker_gsettings_schemas
  configure_file(
    input: schema,
    output: '@PLAINNAME@',
    copy: true,
    install: true,
    install_dir: gsettings_schema_dir)
endforeach

tracker_store_settings_enums = custom_target('tracker-store-settings-enums',
    input: tracker_common_enums_header,
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

# Compile schemas locally so that the functional-tests can use them without
# them being installed into /usr.
#
custom_target('compile-schemas',
  output: 'gschemas.compiled',
  command: [glib_compile_schemas, meson.current_build_dir()],
  build_by_default: true,
  depends: tracker_store_settings_enums,
  depend_files: tracker_gsettings_schemas)

tracker_uninstalled_gsettings_schema_dir = meson.current_build_dir()
