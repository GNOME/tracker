subdir('stop-words')

tracker_common_enums_header = files('tracker-enums.h')

enums = gnome.mkenums('tracker-enum-types',
  sources: tracker_common_enums_header,
  c_template: 'tracker-enum-types.c.template',
  h_template: 'tracker-enum-types.h.template',
)

tracker_common_enum_header = enums[1]

tracker_common_sources = [
  'tracker-date-time.c',
  'tracker-dbus.c',
  'tracker-domain-ontology.c',
  'tracker-file-utils.c',
  'tracker-ioprio.c',
  'tracker-log.c',
  'tracker-sched.c',
  'tracker-type-utils.c',
  'tracker-utils.c',
  'tracker-locale.c',
  'tracker-parser-utils.c',
  'tracker-language.c',
  enums[0], enums[1],
]

if unicode_library_name == 'icu'
  tracker_common_sources += 'tracker-parser-libicu.c'
else
  tracker_common_sources += 'tracker-parser-libunistring.c'
endif

tracker_common_dependencies = [glib, gio, gio_unix, libmath, libstemmer]

if build_machine.system() == 'openbsd'
  libkvm = meson.get_compiler('c').find_library('kvm')
  tracker_common_dependencies += libkvm
endif

libtracker_common = static_library('tracker-common',
  tracker_common_sources,
  dependencies: tracker_common_dependencies + [unicode_library],
  c_args: tracker_c_args,
  include_directories: [configinc, srcinc],
)

commoninc = include_directories('.')

tracker_common_dep = declare_dependency(
  sources: [tracker_common_enum_header],
  link_with: libtracker_common,
  dependencies: tracker_common_dependencies,
  include_directories: [configinc, srcinc, commoninc],
)
