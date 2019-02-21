project('tracker', 'c', 'vala',
        version: '2.2.0',
        meson_version: '>=0.47')

gnome = import('gnome')
i18n = import('i18n')
cc = meson.get_compiler('c')

# This is the X.Y used in -llibtracker-FOO-X.Y
tracker_api_version = '2.0'

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

glib_required = '2.46.0'

# 3.6.11 for sqlite_backup API
# 3.6.16 to fix test failures
# 3.6.17 for shared cache mode with virtual tables
# 3.7.0 for WAL
# 3.7.9 for FTS4 content= support
# 3.7.15 for sqlite3_errstr() support
# 3.8.3 for SQLITE_DETERMINISTIC
sqlite_required = '3.8.3'

dbus = dependency('dbus-1', version: '> 1.3.1')
gio = dependency('gio-2.0', version: '>' + glib_required)
gio_unix = dependency('gio-unix-2.0', version: '>' + glib_required)
glib = dependency('glib-2.0', version: '>' + glib_required)
gmodule = dependency('gmodule-2.0', version: '>' + glib_required)
gobject = dependency('gobject-2.0', version: '>' + glib_required)
gobject_introspection = dependency('gobject-introspection-1.0')
icu_i18n = dependency('icu-i18n', version: '> 4.8.1.1', required: false)
icu_uc = dependency('icu-uc', version: '> 4.8.1.1', required: false)
json_glib = dependency('json-glib-1.0', version: '>= 1.0', required: true)
libsoup = dependency('libsoup-2.4', version: '> 2.40', required: true)
libxml2 = dependency('libxml-2.0', version: '> 2.6')
sqlite = dependency('sqlite3', version: '>' + sqlite_required)

libmath = cc.find_library('m', required: false)

# FIXME: here we work around a quirk todo with Meson and Vala: the 'uuid'
# pkg-config component corresponds to 'libuuid.vapi', but Meson assumes that
# .vapi name always matches pkg-config name, and will add `--pkg uuid` to the
# Vala commandline. By finding 'uuid' using find_library() instead, we get
# a dependency object back that won't cause any `--pkg` arguments to be added
# to the Vala commandline, but still works as expected for C code.
uuid = cc.find_library('uuid')

add_project_arguments('-Wformat', '-Wformat-security', language: 'c')

# There are various gchar vs. unsigned char warnings that occur in extract
# modules, it's not worth adding casts everywhere so we disable the warning.
add_project_arguments('-Wno-pointer-sign', language: 'c')

add_project_arguments('-DTRACKER_COMPILATION',
                      '-DG_LOG_DOMAIN="Tracker"',
                      language: 'c')
add_project_arguments(['-D', 'TRACKER_COMPILATION'],
                      ['-D', 'G_LOG_DOMAIN="Tracker"'],
                      language: 'vala')

enable_fts = get_option('fts')
sqlite3_fts5 = get_option('sqlite3_fts5')

network_manager = dependency('libnm', required: get_option('network_manager'))
have_network_manager = network_manager.found()

##################################################################
# Check for libtracker-common, make sure libstemmer exists
##################################################################

libstemmer = cc.find_library('stemmer', required: get_option('stemmer'))
have_libstemmer = libstemmer.found()

##################################################################
# Check for tracker-fts, allow disabling FTS support
##################################################################

if enable_fts
  if meson.is_cross_build() and not meson.has_exe_wrapper()
	if sqlite3_fts5 == 'auto'
	  error('Can\'t detect whether sqlite3 has builtin fts5. please set sqlite3_fts5 accordingly.')
    elif sqlite3_fts5 == 'builtin'
	  sqlite3_has_builtin_fts5 = true
    elif sqlite3_fts5 == 'external'
	  sqlite3_has_builtin_fts5 = false
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
      if sqlite.version().version_compare('>= 3.20.0')
        error('sqlite3 >= 3.20.0 must be compiled with --enable-fts5 in order to get FTS support.')
      else
        message('sqlite3 is older than version 3.20.0, using FTS module that is bundled with Tracker')
        sqlite3_has_builtin_fts5 = false
      endif
    endif
  endif

  if sqlite3_has_builtin_fts
    message('Using sqlite3 builtin FTS module')
  else
    message('FTS support was enabled but SQLite doesn\'t have the FTS module built in')
  endif

else
  sqlite3_has_builtin_fts5 = false
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

if get_option('dbus_services') == ''
  dbus_services_dir = dbus.get_pkgconfig_variable('session_bus_services_dir',
                                                  define_variable: [ 'datadir', datadir ])
else
  dbus_services_dir = get_option('dbus_services')
endif

####################################################################
# systemd user services
####################################################################

install_systemd_user_services = false
if get_option('systemd_user_services') == 'yes' or get_option('systemd_user_services') == 'auto'
  systemd = dependency('systemd', required: false)
  if systemd.found()
    # FIXME: this would ideally use the systemduserunitdir pkgconfig variable, but
    # it does not depend on variables we can override to install within prefix.
    install_systemd_user_services = true
    systemd_user_services_dir = join_paths(libdir, 'systemd', 'user')
  else
    install_systemd_user_services = false
    systemd_user_services_dir = '(not found)'
  endif
elif get_option('systemd_user_services') == 'no' or get_option('systemd_user_services') == ''
  install_systemd_user_services = false
  systemd_user_services_dir = '(disabled)'
else
  install_systemd_user_services = true
  systemd_user_services_dir = get_option('systemd_user_services')
endif

####################################################################
# bash-completion
####################################################################

install_bash_completion = false
if get_option('bash_completion') == 'yes' or get_option('bash_completion') == 'auto'
  bash_completion_package = dependency('bash-completion', required: false)
  if bash_completion_package.found()
    bash_completion_dir = bash_completion_package.get_pkgconfig_variable('completionsdir',
                                                                         define_variable:  [ 'prefix', get_option('prefix') ])
  else
    bash_completion_dir = join_paths(datadir, 'bash-completion', 'completions')
  endif
  install_bash_completion = true
elif get_option('bash_completion') == 'no' or get_option('bash_completion') == ''
  install_bash_completion = false
  bash_completion_dir = '(disabled)'
else
  install_bash_completion = true
  bash_completion_dir = get_option('bash_completion')
endif

conf = configuration_data()

# Config that goes in config.h
conf.set('DISABLE_JOURNAL', get_option('journal') == false)

conf.set10('HAVE_TRACKER_FTS', enable_fts)

conf.set('HAVE_BUILTIN_FTS', sqlite3_has_builtin_fts5)
conf.set('HAVE_LIBICU', unicode_library_name == 'icu')
conf.set('HAVE_LIBSTEMMER', have_libstemmer)
conf.set('HAVE_LIBUNISTRING', unicode_library_name == 'unistring')
conf.set('HAVE_NETWORK_MANAGER', have_network_manager)

conf.set('HAVE_GETLINE', cc.has_function('getline', prefix : '#include <stdio.h>'))
conf.set('HAVE_POSIX_FADVISE', cc.has_function('posix_fadvise', prefix : '#include <fcntl.h>'))
conf.set('HAVE_STATVFS64', cc.has_header_symbol('sys/statvfs.h', 'statvfs64', args: '-D_LARGEFILE64_SOURCE'))
conf.set('HAVE_STRNLEN', cc.has_function('strnlen', prefix : '#include <string.h>'))

conf.set('LOCALEDIR', '"@0@/@1@"'.format(get_option('prefix'), get_option('localedir')))
conf.set('SHAREDIR', '"@0@/@1@"'.format(get_option('prefix'), get_option('datadir')))
conf.set('TRACKER_MINERS_DIR', '"@0@/@1@/tracker/miners"'.format(get_option('prefix'), get_option('datadir')))

conf.set('GETTEXT_PACKAGE', '"tracker"')
conf.set('PACKAGE_VERSION', '"@0@"'.format(meson.project_version()))
conf.set('TRACKER_VERSION', '"@0@"'.format(meson.project_version()))
conf.set('TRACKER_MAJOR_VERSION', tracker_major_version)
conf.set('TRACKER_MINOR_VERSION', tracker_minor_version)
conf.set('TRACKER_MICRO_VERSION', tracker_micro_version)
conf.set('TRACKER_INTERFACE_AGE', 0)
conf.set('TRACKER_BINARY_AGE', 100 * tracker_minor_version + tracker_micro_version)

# Config that goes in some other generated files (.desktop, .pc, etc)
conf.set('exec_prefix', get_option('prefix'))
conf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
conf.set('datadir', datadir)
conf.set('datarootdir', join_paths(get_option('prefix'), get_option('datadir')))
conf.set('includedir', join_paths(get_option('prefix'), get_option('includedir')))
conf.set('libdir', libdir)
conf.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))
conf.set('prefix', get_option('prefix'))
conf.set('TRACKER_API_VERSION', tracker_api_version)
conf.set('VERSION', meson.project_version())
conf.set('tracker_store', join_paths ('${libexecdir}', 'tracker-store'))
conf.set('ontologies_dir', join_paths ('${datadir}', 'tracker', 'ontologies'))
conf.set('domain_ontologies_dir', join_paths('${datadir}', 'tracker', 'domain-ontologies'))

# Configure functional tests to run completely from source tree.
conf.set('FUNCTIONAL_TESTS_ONTOLOGIES_DIR', join_paths(meson.current_source_dir(), 'tests', 'functional-tests', 'test-ontologies'))
conf.set('FUNCTIONAL_TESTS_TRACKER_STORE_PATH', join_paths(meson.current_build_dir(), 'src', 'tracker-store', 'tracker-store'))

# This is set in an awkward way for compatibility with Autoconf. Switch it
# to a normal boolean once we get rid of the Autotools build system. It's
# only used in tests/functional-tests/common/utils/configuration.py.in.
if get_option('journal')
    conf.set('DISABLE_JOURNAL_TRUE', 'true')
else
    conf.set('DISABLE_JOURNAL_TRUE', '')
endif

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

gsettings_schema_dir = join_paths(get_option('datadir'), 'glib-2.0', 'schemas')

tracker_internal_libs_dir = join_paths(get_option('prefix'), get_option('libdir'), 'tracker-' + tracker_api_version)

# NOTE: We don't use ${TRACKER_API_VERSION} because other content like
# the ontology is installed to the same location.
tracker_ontologies_dir = join_paths(get_option('prefix'), get_option('datadir'), 'tracker/ontologies')

vapi_dir = join_paths(get_option('prefix'), get_option('datadir'), 'vala', 'vapi')

glib_mkenums = find_program('glib-mkenums')
g_ir_compiler = find_program('g-ir-compiler', gobject_introspection.get_pkgconfig_variable('g_ir_compiler'))
g_ir_merge = find_program('g-ir-merge', join_paths(meson.current_source_dir(), 'utils', 'g-ir-merge', 'g-ir-merge'))

gir_dir = gobject_introspection.get_pkgconfig_variable('girdir',
                                                       define_variable:  [ 'datadir', datadir ])
typelib_dir = gobject_introspection.get_pkgconfig_variable('typelibdir',
                                                           define_variable:  [ 'libdir', libdir ])

subdir('src')
subdir('data')
subdir('docs')
subdir('examples')
subdir('utils')

test_c_args = tracker_c_args + [
    '-DTOP_BUILDDIR="@0@"'.format(build_root),
    '-DTOP_SRCDIR="@0@"'.format(source_root),
]

tracker_uninstalled_domain_rule = join_paths(meson.current_source_dir(), 'src', 'tracker-store', 'default.rule')
tracker_uninstalled_nepomuk_ontologies_dir = join_paths(meson.current_source_dir(), 'src', 'ontologies', 'nepomuk')
tracker_uninstalled_stop_words_dir = join_paths(meson.current_source_dir(), 'src', 'libtracker-common', 'stop-words')

subdir('tests')

subdir('po')

# Make sure we're not using versions of SQLite which cause problems.
#
# Avoid versions:
sqlite_safe = true
if sqlite.version().version_compare('>= 3.7.10') and sqlite.version().version_compare('<= 3.7.13')
  # See https://mail.gnome.org/archives/tracker-list/2012-October/msg00028.html
  sqlite_safe = false
elif sqlite.version().version_compare('3.8.1')
  # See https://mail.gnome.org/archives/tracker-list/2013-November/msg00021.html
  sqlite_safe = false
elif sqlite.version().version_compare('3.8.4.2')
  # See https://mail.gnome.org/archives/tracker-list/2014-April/msg00001.html
  sqlite_safe = false
endif

summary = [
  '\nBuild Configuration:',
  '    Prefix:                                 ' + get_option('prefix'),
  '    Source code location:                   ' + meson.current_source_dir(),
  '    Compiler:                               ' + cc.get_id(),
  '\nFeature Support:',
  '    Support for network status detection:   ' + have_network_manager.to_string(),
  '    Unicode support library:                ' + unicode_library_name,
  '    Build with Journal support:             ' + get_option('journal').to_string(),
  '    Build with SQLite FTS support:          @0@ (built-in FTS: @1@)'.format(
     get_option('fts'), sqlite3_has_builtin_fts5),
  '    Build with Stemming support:            ' + have_libstemmer.to_string(),
  '    Bash completion support:                ' + bash_completion_dir,
]

message('\n'.join(summary))

if not sqlite_safe
  message('''WARNING:
        SQLite3 version @0@ is not safe. For detail
        3.7.10 - 3.7.13: https://mail.gnome.org/archives/tracker-list/2012-October/msg00028.html
        3.8.1:           https://mail.gnome.org/archives/tracker-list/2013-November/msg00021.html
        3.8.4.2:         https://mail.gnome.org/archives/tracker-list/2014-April/msg00001.html
  ''').format(sqlite.version())
endif
