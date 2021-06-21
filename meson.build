project('tracker', 'c', 'vala',
        version: '3.2.0.alpha.1',
        meson_version: '>=0.57',
        default_options: [
          'c_std=c99',
          'warning_level=2'])

fs = import('fs')
gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')
cc = meson.get_compiler('c')

# This is the X.Y used in -llibtracker-FOO-X.Y
tracker_api_version = '3.0'
tracker_api_major = tracker_api_version.split('.')[0]
tracker_versioned_name = 'tracker@0@'.format(tracker_api_major)

tracker_version = meson.project_version().split('-')[0]
tracker_major_version = tracker_version.split('.')[0].to_int()
tracker_minor_version = tracker_version.split('.')[1].to_int()
tracker_micro_version = tracker_version.split('.')[2].to_int()
tracker_interface_age = 0
tracker_binary_age = 100 * tracker_minor_version + tracker_micro_version - tracker_interface_age

# This the .Z used in libtracker-sparql-2.0.so.Z
soversion = '0'
libversion = '@0@.@1@.@2@'.format(soversion, tracker_binary_age - tracker_interface_age, tracker_interface_age)

libdir = join_paths(get_option('prefix'), get_option('libdir'))
datadir = join_paths(get_option('prefix'), get_option('datadir'))

glib_required = '2.52.0'

# 3.6.11 for sqlite_backup API
# 3.6.16 to fix test failures
# 3.6.17 for shared cache mode with virtual tables
# 3.7.0 for WAL
# 3.7.9 for FTS4 content= support
# 3.7.15 for sqlite3_errstr() support
# 3.8.3 for SQLITE_DETERMINISTIC
# 3.15.0 for https://sqlite.org/rowvalue.html
sqlite_required = '3.15.0'

gio = dependency('gio-2.0', version: '>' + glib_required)
gio_unix = dependency('gio-unix-2.0', version: '>' + glib_required)
glib = dependency('glib-2.0', version: '>' + glib_required)
gmodule = dependency('gmodule-2.0', version: '>' + glib_required)
gobject = dependency('gobject-2.0', version: '>' + glib_required)
gobject_introspection = dependency('gobject-introspection-1.0', required: get_option('introspection'))
icu_i18n = dependency('icu-i18n', version: '> 4.8.1.1', required: false)
icu_uc = dependency('icu-uc', version: '> 4.8.1.1', required: false)
json_glib = dependency('json-glib-1.0', version: '>= 1.4', required: true)
libsoup = dependency('libsoup-2.4', version: '> 2.40', required: true)
libxml2 = dependency('libxml-2.0', version: '> 2.6')
sqlite = dependency('sqlite3', version: '>' + sqlite_required)
dbus = dependency('dbus-1')

if get_option('soup2')
  libsoup = dependency('libsoup-2.4', version: '> 2.40', required: true)
else
  libsoup = dependency('libsoup-3.0', version: '>= 2.99.2', required: true)
endif

libmath = cc.find_library('m', required: false)

if get_option('man')
  asciidoc = find_program('asciidoc')
  xsltproc = find_program('xsltproc')
endif

py_modules = ['gi']
if get_option('tests_tap_protocol')
  py_modules += 'tap'
endif
python = import('python').find_installation('python3', modules: py_modules)

cc_warning_flags = [
  '-Wformat',
  '-Wformat-security',
  # There are numerous switches over data or token types in the
  # codebase. This makes it easy to verify that all are handled
  # correctly. -Wswitch-enum could be even better, but there are a few
  # enums with hundreds of entries that are only partially covered in
  # some of the switches.
  '-Wswitch',
  '-Werror=switch',
  # Too many warnings in libtracker-data/tracker-sparql-grammar.h
  '-Wno-tautological-constant-out-of-range-compare',
  # This probably deserves a better look.
  '-Wno-missing-field-initializers',
  # Too many unused parameters in tests. Disabling this warning only
  # for the tests executables could be a better solution.
  '-Wno-unused-parameter',
]
add_project_arguments(cc.get_supported_arguments(cc_warning_flags), language: 'c')

add_project_arguments('-DTRACKER_COMPILATION',
                      '-DG_LOG_DOMAIN="Tracker"',
                      '-DG_LOG_STRUCTURED=1',
                      language: 'c')
add_project_arguments(['-D', 'TRACKER_COMPILATION'],
                      ['-D', 'G_LOG_DOMAIN="Tracker"'],
                      ['-D', 'G_LOG_STRUCTURED=1'],
                      language: 'vala')

if get_option('buildtype') == 'plain'
  # Default case is to allow TRACKER_DEBUG which is very useful for
  # triaging user-reported issues.
  # Use --buildtype=release to disable debug completely.
  debug = true
else
  debug = get_option('debug')
endif
optimization = get_option('optimization')

debug_cflags = []
if debug
  debug_cflags += '-DG_ENABLE_DEBUG'
  if optimization in ['0', 'g']
    debug_cflags += '-DG_ENABLE_CONSISTENCY_CHECKS'
  endif
elif optimization in ['2', '3', 's']
  debug_cflags += '-DG_DISABLE_CAST_CHECKS'
endif

add_project_arguments(debug_cflags, language: 'c')


####################################################################
# SQLite
####################################################################

sqlite3_versions_known_bad = configuration_data()
text = fs.read('sqlite3_versions_known_bad')
foreach line : text.split('\n')
  parts = line.split(' ')
  if parts.length() == 2
    version = parts[0]
    link = parts[1]
    sqlite3_versions_known_bad.set(version, link)
  endif
endforeach

if sqlite3_versions_known_bad.has(sqlite.version())
  if get_option('override_sqlite_version_check')
    warn('SQlite3 version @0@ is known bad. Ignoring due to -Doverride_sqlite_version_check=true'.format(
      sqlite.version()))
  else
    link = sqlite3_versions_known_bad.get(sqlite.version())
    error('SQLite3 version is unsafe (@0@).\nSee @1@ for details.\nYou can bypass this check with -Doverride_sqlite_version_check=true'.format(
      sqlite.version(), link))
  endif
endif

##################################################################
# Check for libtracker-common, make sure libstemmer exists
##################################################################

libstemmer = cc.find_library('stemmer', required: get_option('stemmer'))
have_libstemmer = libstemmer.found()

if meson.is_cross_build() and not meson.has_exe_wrapper()
  sqlite3_fts5 = meson.get_cross_property('sqlite3_has_fts5')
  if sqlite3_fts5 == ''
    error('Please assign an appropriate value for sqlite3_has_fts5 in the [properties] section of your crossfile')
  elif sqlite3_fts5 == 'true'
    sqlite3_has_builtin_fts5 = true
  elif sqlite3_fts5 == 'false'
    sqlite3_has_builtin_fts5 = false
  else
    error('Invalid value of sqlite3_has_fts5 property, use \'true\' or \'false\'')
  endif
else
  sqlite3_builtin_fts5_test = '''
    #include <sqlite3.h>

    int main (int argc, char *argv[]) {
      sqlite3 *db;
      int rc;
      rc = sqlite3_open(":memory:", &db);
      if (rc!=SQLITE_OK) return -1;
      rc = sqlite3_exec(db, "create table a(text)", 0, 0, 0);
      if (rc!=SQLITE_OK) return -1;
      rc = sqlite3_exec(db, "create virtual table t using fts5(content='a',text)", 0, 0, 0);
      if (rc!=SQLITE_OK) return -1;
    }
  '''

  result = cc.run(sqlite3_builtin_fts5_test,
    name: 'sqlite3 has builtin FTS5 module',
    dependencies: sqlite)

  if not result.compiled()
    error('Failed to compile SQLite FTS test.')
  endif

  if result.returncode() == 0
    sqlite3_has_builtin_fts5 = true
  else
    sqlite3_has_builtin_fts5 = false
  endif
endif

if sqlite3_has_builtin_fts5
  message('Using sqlite3 builtin FTS module')
elif sqlite.version().version_compare('>= 3.20.0')
  error('FTS support was enabled but SQLite doesn\'t have the FTS module built in')
else
  message('sqlite3 is older than version 3.20.0, using FTS module that is bundled with Tracker')
endif

##################################################################
# Check for libtracker-data and libtracker-fts: Unicode support
#
# By default, AUTO with this order of preference:
#  1)  libunistring
#  2)  libicu
##################################################################

have_libicu = false
unicode_library_name = ''

if icu_i18n.found() and icu_uc.found()
  have_libicu = true
endif

if have_libicu
  if get_option('unicode_support') == 'auto' or get_option('unicode_support') == 'icu'
    unicode_library = declare_dependency(
        dependencies: [icu_uc, icu_i18n]
    )
    unicode_library_name = 'icu'
  endif
elif get_option('unicode_support') == 'icu'
  error('libicu explicitly requested, but icu-i18n and icu-uc libraries were not found')
endif

if unicode_library_name == ''
  libunistring = cc.find_library('unistring', required: false)

  if libunistring.found()
    unicode_library = libunistring
    unicode_library_name = 'unistring'
  elif get_option('unicode_support') == 'unistring'
    error('libunistring explicitly requested, but not found')
  else
    error('Neither libicu or libunistring were found for Unicode support.')
  endif
endif

####################################################################
# D-Bus service files
####################################################################

if get_option('dbus_services_dir') == ''
  dbus_services_dir = dbus.get_pkgconfig_variable('session_bus_services_dir',
                                                  define_variable: [ 'datadir', datadir ])
else
  dbus_services_dir = get_option('dbus_services_dir')
endif

####################################################################
# systemd user services
####################################################################

install_systemd_user_services = get_option('systemd_user_services')
if install_systemd_user_services
  systemd_user_services_dir = get_option('systemd_user_services_dir')

  if systemd_user_services_dir == ''
    systemd = dependency('systemd', version: '>= 242', required: false)
    if systemd.found()
      systemd_user_services_dir = systemd.get_pkgconfig_variable('systemduserunitdir',
                                                                 define_variable: ['prefix', get_option('prefix')])
    else
      error('systemd user services were enabled, but systemd was not found. ' +
            'Please set the systemd_user_services_dir option if you want to ' +
            'install them, or set systemd_user_services=false to disable.')
    endif
  endif
endif

####################################################################
# bash-completion
####################################################################

if get_option('bash_completion')
  bash_completion_dir = get_option('bash_completion_dir')

  if bash_completion_dir == ''
    bash_completion_package = dependency('bash-completion', required: false)
    if bash_completion_package.found()
      bash_completion_dir = bash_completion_package.get_pkgconfig_variable('completionsdir',
                                                                           define_variable: [ 'prefix', get_option('prefix') ])
    else
      bash_completion_dir = join_paths(datadir, 'bash-completion', 'completions')
    endif
  endif
endif

conf = configuration_data()

# Config that goes in config.h

conf.set('HAVE_BUILTIN_FTS', sqlite3_has_builtin_fts5)
conf.set('HAVE_LIBICU', unicode_library_name == 'icu')
conf.set('HAVE_LIBSTEMMER', have_libstemmer)
conf.set('HAVE_LIBUNISTRING', unicode_library_name == 'unistring')

conf.set('HAVE_POSIX_FADVISE', cc.has_function('posix_fadvise', prefix : '#include <fcntl.h>'))
conf.set('HAVE_STATVFS64', cc.has_header_symbol('sys/statvfs.h', 'statvfs64', args: '-D_LARGEFILE64_SOURCE'))

conf.set('LOCALEDIR', '"@0@/@1@"'.format(get_option('prefix'), get_option('localedir')))
conf.set('SHAREDIR', '"@0@/@1@"'.format(get_option('prefix'), get_option('datadir')))

conf.set('GETTEXT_PACKAGE', '"@0@"'.format(tracker_versioned_name))
conf.set('PACKAGE_VERSION', '"@0@"'.format(meson.project_version()))
conf.set('TRACKER_VERSION', '"@0@"'.format(meson.project_version()))
conf.set('TRACKER_MAJOR_VERSION', tracker_major_version)
conf.set('TRACKER_MINOR_VERSION', tracker_minor_version)
conf.set('TRACKER_MICRO_VERSION', tracker_micro_version)
conf.set('TRACKER_INTERFACE_AGE', 0)
conf.set('TRACKER_BINARY_AGE', 100 * tracker_minor_version + tracker_micro_version)

# Config that goes in some other generated files (.desktop, .service, etc)
conf.set('abs_top_builddir', meson.current_build_dir())
conf.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(input: 'config.h.meson.in',
               output: 'config.h',
               configuration: conf)

# Global compiler defines. We avoid add_global_arguments() as it breaks
# when we are included by another project as a subproject.
tracker_c_args = [
  # Needed for O_NOATIME, and probably other stuff.
  '-D_GNU_SOURCE',

  # Needed for statvfs64, and probably other stuff
  '-D_LARGEFILE64_SOURCE',
]

configinc = include_directories('./')
srcinc = include_directories('src/')

source_root = meson.current_source_dir()
build_root = meson.current_build_dir()
po_subdir = join_paths(source_root, 'po')

tracker_internal_libs_dir = join_paths(get_option('prefix'), get_option('libdir'), 'tracker-' + tracker_api_version)

# NOTE: We don't use ${TRACKER_API_VERSION} because other content like
# the ontology is installed to the same location.
tracker_ontologies_dir = join_paths(get_option('prefix'), get_option('datadir'), tracker_versioned_name, 'ontologies')

vapi_dir = join_paths(get_option('prefix'), get_option('datadir'), 'vala', 'vapi')

if gobject_introspection.found()
  typelib_dir = gobject_introspection.get_pkgconfig_variable('typelibdir',
                                                           define_variable:  [ 'libdir', libdir ])
endif

subdir('src')
subdir('docs')
subdir('utils')

test_c_args = tracker_c_args + [
    '-DTOP_BUILDDIR="@0@"'.format(build_root),
    '-DTOP_SRCDIR="@0@"'.format(source_root),
]

tracker_uninstalled_cli_dir = join_paths(meson.current_build_dir(), 'src', 'tracker')
tracker_uninstalled_nepomuk_ontologies_dir = join_paths(meson.current_source_dir(), 'src', 'ontologies', 'nepomuk')
tracker_uninstalled_stop_words_dir = join_paths(meson.current_source_dir(), 'src', 'libtracker-common', 'stop-words')
tracker_uninstalled_testutils_dir = join_paths(meson.current_source_dir(), 'utils')

subdir('tests')
subdir('examples')

subdir('po')

summary = [
  '\nBuild Configuration:',
  '    Prefix:                                 ' + get_option('prefix'),
  '    Source code location:                   ' + meson.current_source_dir(),
  '    Debug:                                  ' + get_option('debug').to_string(),
  '    Optimization:                           ' + get_option('optimization'),
  '    Compiler:                               ' + cc.get_id(),
  '\nFeature Support:',
  '    Unicode support library:                ' + unicode_library_name,
  '    Use external FTS module:                ' + (not sqlite3_has_builtin_fts5).to_string(),
  '    Build with Stemming support:            ' + have_libstemmer.to_string(),
]

if get_option('bash_completion')
  summary += [
    '    Bash completion support:                ' + bash_completion_dir,
  ]
endif


message('\n'.join(summary))
