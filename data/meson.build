schemas = [
  'org.freedesktop.Tracker.DB.gschema.xml',
  'org.freedesktop.Tracker.FTS.gschema.xml',
  'org.freedesktop.Tracker.gschema.xml',
  'org.freedesktop.Tracker.Store.gschema.xml'
]

foreach schema : schemas
  schemas = configure_file(
    input: schema,
    output: schema,
    copy: true,
    install: true,
    install_dir: gsettings_schema_dir)
endforeach

tracker_store_settings_enums = custom_target('tracker-store-settings-enums',
    input: '../src/libtracker-common/tracker-enums.h',
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

# for unit tests
custom_target('compile-schemas',
  output: 'gschemas.compiled',
  command: [find_program('glib-compile-schemas'), meson.current_build_dir()],
  build_by_default: true,
  depends: tracker_store_settings_enums)
