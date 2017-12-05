# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
# This file is part of Tracker.

AC_PREREQ([2.64])

# Making releases:
#   TRACKER_MICRO_VERSION += 1;
#   TRACKER_INTERFACE_AGE += 1;
#   TRACKER_BINARY_AGE += 1;
# if any functions have been added, set TRACKER_INTERFACE_AGE to 0.
# if backwards compatibility has been broken,
# set TRACKER_BINARY_AGE and TRACKER_INTERFACE_AGE to 0.
m4_define([tracker_major_version], [2])
m4_define([tracker_minor_version], [0])
m4_define([tracker_micro_version], [2])
m4_define([tracker_interface_age], [0])
m4_define([tracker_binary_age],
          [m4_eval(100 * tracker_minor_version + tracker_micro_version)])
m4_define([tracker_version],
          [tracker_major_version.tracker_minor_version.tracker_micro_version])
# This is the X.Y used in -llibtracker-FOO-X.Y
m4_define([tracker_api_version], [2.0])

# libtool version related macros
m4_define([tracker_lt_release], [tracker_major_version.tracker_minor_version])
m4_define([tracker_lt_current],
          [m4_eval(100 * tracker_minor_version + tracker_micro_version - tracker_interface_age)])
m4_define([tracker_lt_revision], [tracker_interface_age])
m4_define([tracker_lt_age], [m4_eval(tracker_binary_age - tracker_interface_age)])
m4_define([tracker_lt_current_minus_age],
          [m4_eval(tracker_lt_current - tracker_lt_age)])

# Define a string for the earliest version that this release has
# backwards binary compatibility with for all interfaces a module
# might. Unless we add module-only API with lower stability
# guarantees, this should be unchanged until we break binary compat
# for Tracker.
#
# TRACKER_BINARY_VERSION=$TRACKER_MAJOR_VERSION.$TRACKER_MINOR_VERSION.$LT_CURRENT
m4_define([tracker_binary_version], [1.0.0])

AC_INIT([tracker],
        [tracker_version],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=tracker],
        [tracker],
        [https://wiki.gnome.org/Projects/Tracker])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/tracker-store/tracker-main.vala])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

# Available since autoconf 2.60, implies _GNU_SOURCE and extensions if available.
# See: https://www.gnu.org/software/autoconf/manual/autoconf-2.64/html_node/Posix-Variants.html
AC_USE_SYSTEM_EXTENSIONS

# Adding the 'subdir-objects' Automake option here will fix some Automake
# warnings, but it will also break the build. I believe that it triggers
# <http://debbugs.gnu.org/cgi/bugreport.cgi?bug=13928>. That bug will
# be fixed in Automake 1.16, so we should be eventually able to use
# 'subdir-objects' (which is apparently will be on by default in Automake 2.0),
# but we'll need to require Automake 1.16 before we can do so.
AM_INIT_AUTOMAKE([1.11 no-define tar-ustar -Wno-portability no-dist-gzip dist-xz])

AC_PROG_LIBTOOL

AM_SILENT_RULES([yes])

dnl http://people.gnome.org/~walters/docs/build-api.txt
dnl We don't support srcdir != builddir.
echo \#buildapi-variable-no-builddir >/dev/null

IDT_COMPILE_WARNINGS

TRACKER_MAJOR_VERSION=tracker_major_version
TRACKER_MINOR_VERSION=tracker_minor_version
TRACKER_MICRO_VERSION=tracker_micro_version
TRACKER_INTERFACE_AGE=tracker_interface_age
TRACKER_BINARY_AGE=tracker_binary_age
TRACKER_VERSION=tracker_version
TRACKER_API_VERSION=tracker_api_version
TRACKER_BINARY_VERSION=tracker_binary_version

dnl Note: this creates TRACKER_API_VERSION_UNDERSCORES and AC_SUBST's it
AX_DOTS_TO_UNDERSCORES(TRACKER_API_VERSION, tracker_api_version)

AC_SUBST(TRACKER_MAJOR_VERSION)
AC_SUBST(TRACKER_MINOR_VERSION)
AC_SUBST(TRACKER_MICRO_VERSION)
AC_SUBST(TRACKER_INTERFACE_AGE)
AC_SUBST(TRACKER_BINARY_AGE)
AC_SUBST(TRACKER_API_VERSION)
AC_SUBST(TRACKER_VERSION)
AC_SUBST(TRACKER_BINARY_VERSION)

AC_DEFINE(TRACKER_MAJOR_VERSION, [tracker_major_version],
          [Define to the Tracker major version])
AC_DEFINE(TRACKER_MINOR_VERSION, [tracker_minor_version],
          [Define to the Tracker minor version])
AC_DEFINE(TRACKER_MICRO_VERSION, [tracker_micro_version],
          [Define to the Tracker micro version])
AC_DEFINE(TRACKER_INTERFACE_AGE, [tracker_interface_age],
          [Define to the Tracker interface age])
AC_DEFINE(TRACKER_BINARY_AGE, [tracker_binary_age],
          [Define to the Tracker binary age])

# libtool versioning
LT_RELEASE=tracker_lt_release
LT_CURRENT=tracker_lt_current
LT_REVISION=tracker_lt_revision
LT_AGE=tracker_lt_age
LT_CURRENT_MINUS_AGE=tracker_lt_current_minus_age
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
AC_SUBST(LT_CURRENT_MINUS_AGE)

# Checks for programs.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lm':
AC_CHECK_LIB([m], [main])

AC_CHECK_HEADERS([fcntl.h float.h inttypes.h limits.h locale.h stddef.h stdint.h stdlib.h string.h strings.h sys/file.h sys/ioctl.h sys/param.h sys/statfs.h sys/statvfs.h sys/time.h unistd.h])

AC_CHECK_HEADER([zlib.h],
                [],
                [AC_MSG_ERROR([You must have zlib.h and zlib installed])])

AC_HEADER_STDC

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Can posix_fadvise be used
AC_CHECK_DECLS(posix_fadvise, [], [], [
#define _XOPEN_SOURCE 600
#include <fcntl.h>])

# Checks for functions
AC_CHECK_FUNCS([posix_fadvise])
AC_CHECK_FUNCS([getline strnlen])

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRNLEN
AC_FUNC_STRTOD
AC_CHECK_FUNCS([alarm ftruncate memchr memmove memset modf munmap setlocale sqrt strcasecmp strchr strcspn strndup strrchr strstr strtoul strtoull tzset])

# if statvfs64() is available, enable the 64-bit API extensions
AC_CHECK_FUNCS([statvfs64], [have_statvfs64=yes], [have_statvfs64=no])
if test "x$have_statvfs" = "xyes" ; then
   CFLAGS="$CFLAGS -D_LARGEFILE64_SOURCE"
fi

# Check for defines we expect
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
#include "stdio.h"
printf ("%s", __FUNCTION__);
   ]])],
   [have_function=yes],
   [have_function=no])

if test "x$have_function" = "xno" ; then
   AC_DEFINE(__FUNCTION__,
             __func__,
            [Defined for compilers not supporting __FUNCTION__])
fi

case $host in
    *-*-linux*)
        tracker_os_linux=yes
        ;;
esac

# Remember CFLAGS upon entering configure...
CFLAGS="$CFLAGS"

# Initialize GLib
GLIB_GSETTINGS
GLIB_TESTS

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

# Binary required versions
PYTHON_REQUIRED=2.6

# Library required versions
DBUS_REQUIRED=1.3.1
GLIB_REQUIRED=2.44.0
LIBXML2_REQUIRED=2.6
UPOWER_REQUIRED=0.9.0
LIBICU_REQUIRED=4.8.1.1
JSON_GLIB_REQUIRED=1.0
LIBSOUP_REQUIRED=2.40

# 3.6.11 for sqlite_backup API
# 3.6.16 to fix test failures
# 3.6.17 for shared cache mode with virtual tables
# 3.7.0 for WAL
# 3.7.9 for FTS4 content= support
# 3.7.15 for sqlite3_errstr() support
# 3.8.4 for SQLITE_DETERMINISTIC
SQLITE_REQUIRED=3.8.4

# Needed to generate .gir files,
# see http://live.gnome.org/GnomeGoals/AddGObjectIntrospectionSupport
GOBJECT_INTROSPECTION_CHECK([0.9.5])

GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
AC_SUBST(GLIB_PREFIX)

# Check requirements for gvdb
GVDB_REQUIRED="glib-2.0 >= $GLIB_REQUIRED"
PKG_CHECK_MODULES(GVDB, [$GVDB_REQUIRED])

# Check requirements for libtracker-bus
LIBTRACKER_BUS_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                         gio-unix-2.0 >= $GLIB_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_BUS, [$LIBTRACKER_BUS_REQUIRED])

# Check requirements for libtracker-common
LIBTRACKER_COMMON_REQUIRED="glib-2.0        >= $GLIB_REQUIRED
                            gio-unix-2.0    >= $GLIB_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_COMMON, [$LIBTRACKER_COMMON_REQUIRED])

case $host in
  *-*-openbsd*)
    LIBTRACKER_COMMON_LIBS="$LIBTRACKER_COMMON_LIBS -lkvm"
    ;;
esac

# Check requirements for libtracker-data
LIBTRACKER_DATA_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                          gio-unix-2.0 >= $GLIB_REQUIRED
                          sqlite3      >= $SQLITE_REQUIRED
                          uuid"
PKG_CHECK_MODULES(LIBTRACKER_DATA, [$LIBTRACKER_DATA_REQUIRED])

LIBTRACKER_DATA_LIBS="$LIBTRACKER_DATA_LIBS -lz -lm"

# Check requirements for libtracker-direct
LIBTRACKER_DIRECT_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                            gio-unix-2.0 >= $GLIB_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_DIRECT, [$LIBTRACKER_DIRECT_REQUIRED])

# Check requirements for libtracker-remote
LIBTRACKER_REMOTE_REQUIRED="glib-2.0       >= $GLIB_REQUIRED
                            libsoup-2.4    >= $LIBSOUP_REQUIRED
                            libxml-2.0     >= $LIBXML2_REQUIRED
                            json-glib-1.0  >= $JSON_GLIB_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_REMOTE, [$LIBTRACKER_REMOTE_REQUIRED])

# Check requirements for libtracker-fts
LIBTRACKER_FTS_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                         gio-unix-2.0 >= $GLIB_REQUIRED
                         sqlite3      >= $SQLITE_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_FTS, [$LIBTRACKER_FTS_REQUIRED])

# Check requirements for libtracker-miner
LIBTRACKER_MINER_REQUIRED="glib-2.0        >= $GLIB_REQUIRED
                           gio-unix-2.0    >= $GLIB_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_MINER, [$LIBTRACKER_MINER_REQUIRED])

# Check requirements for libtracker-control
LIBTRACKER_CONTROL_REQUIRED="glib-2.0        >= $GLIB_REQUIRED
                             gio-unix-2.0    >= $GLIB_REQUIRED"

PKG_CHECK_MODULES(LIBTRACKER_CONTROL, [$LIBTRACKER_CONTROL_REQUIRED])

# Check requirements for libtracker-sparql
LIBTRACKER_SPARQL_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                            gio-unix-2.0 >= $GLIB_REQUIRED
                            uuid"

PKG_CHECK_MODULES(LIBTRACKER_SPARQL, [$LIBTRACKER_SPARQL_REQUIRED])

# Check requirements for tracker-control
TRACKER_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                  gio-unix-2.0 >= $GLIB_REQUIRED
                  uuid"

PKG_CHECK_MODULES(TRACKER, [$TRACKER_REQUIRED])

# Check requirements for tracker-resdump
TRACKER_RESDUMP_REQUIRED="glib-2.0     >= $GLIB_REQUIRED"

PKG_CHECK_MODULES(TRACKER_RESDUMP, [$TRACKER_RESDUMP_REQUIRED],
                  [have_tracker_resdump=yes],
                  [have_tracker_resdump=no])

# Check requirements for tracker-store
TRACKER_STORE_REQUIRED="glib-2.0     >= $GLIB_REQUIRED
                        gio-unix-2.0 >= $GLIB_REQUIRED
                        gmodule-2.0  >= $GLIB_REQUIRED
                        sqlite3      >= $SQLITE_REQUIRED"

PKG_CHECK_MODULES(TRACKER_STORE, [$TRACKER_STORE_REQUIRED])

TRACKER_STORE_LIBS="$TRACKER_STORE_LIBS -lz -lm"

# Check we have Vala valac command we need
VALA_MIN_VERSION=0.18.0
AM_PROG_VALAC([$VALA_MIN_VERSION])
if test "x$VALAC" = "x"; then
    AC_MSG_ERROR([Vala requested but valac >= $VALA_MIN_VERSION is not installed])
fi

####################################################################
# Check gettext/intltool support
####################################################################
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, ["$GETTEXT_PACKAGE"], [Define the gettext package to be used])
IT_PROG_INTLTOOL([0.40.0])

####################################################################
# Check if we should enable GCOV coverage reporting support
####################################################################

AC_ARG_ENABLE(gcov,
              AS_HELP_STRING([--enable-gcov],
                             [enable coverage reporting support [[default=no]]]),
              [enable_gcov=$enableval],
              [enable_gcov=no])
AM_CONDITIONAL(ENABLE_GCOV, test "x$enable_gcov" != "xno")

if test "x$enable_gcov" != "xno" ; then
  GCOV_VALAFLAGS="-g"
  GCOV_CFLAGS="-ftest-coverage -fprofile-arcs"
  GCOV_LIBS="-lgcov"
else
  GCOV_VALAFLAGS=
  GCOV_CFLAGS=
  GCOV_LIBS=
fi

AC_SUBST(GCOV_VALAFLAGS)
AC_SUBST(GCOV_CFLAGS)
AC_SUBST(GCOV_LIBS)

####################################################################
# General VALAFLAGS/CFLAGS/LIBS
#
# NOTE: BUILD_VALACFLAGS are for the sources built by Vala,
#       not for .vala files themselves.
####################################################################
BUILD_VALAFLAGS="-g --target-glib=${GLIB_REQUIRED}"
BUILD_VALACFLAGS="${WARN_VALACFLAGS} ${GCOV_CFLAGS} -DG_LOG_DOMAIN=\\\"Tracker\\\" -DTRACKER_COMPILATION"
BUILD_CFLAGS="${WARN_CFLAGS} ${GCOV_CFLAGS} -DG_LOG_DOMAIN=\\\"Tracker\\\" -DTRACKER_COMPILATION"
BUILD_LIBS="${GCOV_LIBS}"

AC_SUBST(BUILD_VALAFLAGS)
AC_SUBST(BUILD_VALACFLAGS)
AC_SUBST(BUILD_CFLAGS)
AC_SUBST(BUILD_LIBS)

####################################################################
# Check if linker supports --enable-new-dtags
####################################################################
AC_MSG_CHECKING([whether $CC supports -Wl,--enable-new-dtags])
save_LDFLAGS=$LDFLAGS
LDFLAGS="-Wl,--enable-new-dtags $LDFLAGS"

AC_LINK_IFELSE([AC_LANG_PROGRAM([],[])],
             [enable_new_dtags=yes; AC_MSG_RESULT(yes)], [enable_new_dtags=no; AC_MSG_RESULT(no)])
LDFLAGS=$save_LDFLAGS
DTAGS_LDFLAGS=
if test "x$enable_new_dtags" = "xyes"; then
    DTAGS_LDFLAGS="-Wl,--enable-new-dtags"
fi
AC_SUBST(DTAGS_LDFLAGS)

####################################################################
# Check for minimal
####################################################################

AC_ARG_ENABLE(minimal,
              AS_HELP_STRING([--enable-minimal],
                             [disabled EVERYTHING except the very core (tracker-store and foundation libraries) [[default=no]]]),
              [enable_minmal=$enableval],
              [enable_minmal=no])

# WE only disable the binaries we're building here, not optional
# extras for those binaries
if test "x$enable_minimal" = "xyes"; then
   AC_MSG_WARN([Building minimal configuration])

   enable_tracker_extract=no
   enable_tracker_writeback=no
   enable_miner_fs=no
   enable_miner_apps=no
   enable_miner_rss=no
fi

####################################################################
# Check for functional-test include/exclude
####################################################################
AC_ARG_ENABLE([functional-tests],
              AS_HELP_STRING([--enable-functional-tests],
                             [enable functional tests [[default=no]]]),
              [enable_functional_tests=$enableval],
              [enable_functional_tests=no])

if test x$enable_functional_tests != "xno"; then
   # Python check, require >= 2.6
   AC_PATH_PROG([PYTHON],[python],[:])
   AS_IF([test "$PYTHON" != ":"],
         [AM_PYTHON_CHECK_VERSION([$PYTHON],[$PYTHON_REQUIRED],[:],[PYTHON=":"])])

   if test "$PYTHON" = ":"; then
      AC_MSG_ERROR([Could not find Python >= $PYTHON_REQUIRED (for functional-tests, try --disable-functional-tests).])
   fi

   AC_DEFINE(DIST_FUNCTIONAL_TESTS, 1, [Include functional tests in the installation])
fi

AM_CONDITIONAL(DIST_FUNCTIONAL_TESTS, test "x$enable_functional_tests" != "xno")

####################################################################
# Check for gtk-doc and docbook-tools
####################################################################

# Check for GTK_DOC_CHECK availability. The GTK_DOC_CHECK invocation
# must be on its own line, gtkdocize relies on it
m4_ifdef([GTK_DOC_CHECK], [
GTK_DOC_CHECK([1.8])

# NOTE: We need to use a separate automake conditional for this
#       to make this work with the tarballs.
AM_CONDITIONAL([ENABLE_GTK_DOC], test "x$enable_gtk_doc" = xyes)
],
[
AM_CONDITIONAL([ENABLE_GTK_DOC], false)
])

####################################################################
# Check if we should install Windows specific binaries
####################################################################

AC_MSG_CHECKING(for WIN32)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
   #ifndef WIN32
   #error
   #endif
   ]])],
   [native_win32=yes; AC_MSG_RESULT(yes)],
   [native_win32=no; AC_MSG_RESULT(no)])

AM_CONDITIONAL(OS_WIN32, test "$native_win32" = "yes")

if test "$native_win32" = "yes" ; then
   AC_DEFINE(OS_WIN32, 1, [Define if we are on win32])
fi

####################################################################
# Check if we should disable the journal
####################################################################

AC_ARG_ENABLE(journal,
              AS_HELP_STRING([--enable-journal],
                             [enable database journal backup mechanism [[default=yes]]]),
              [enable_journal=$enableval],
              [enable_journal=yes])

if test "x$enable_journal" != "xno" ; then
   have_tracker_journal="yes"
else
   have_tracker_journal="no  (disabled)"
fi

AM_CONDITIONAL(DISABLE_JOURNAL, test "x$enable_journal" = "xno")

if test "x$enable_journal" = "xno" ; then
   AC_DEFINE(DISABLE_JOURNAL, 1, [Define if we disable the journal])
fi

####################################################################
# Check for SQLite
####################################################################

PKG_CHECK_MODULES(SQLITE3, [sqlite3 >= $SQLITE_REQUIRED])
AC_SUBST(SQLITE3_CFLAGS)
AC_SUBST(SQLITE3_LIBS)

# Make sure SQLite is compiled thread-safe
AX_SQLITE_THREADSAFE
if test "x$ax_cv_sqlite_threadsafe" != "xyes"; then
  AC_MSG_ERROR([sqlite3 is not compiled thread-safe])
fi

# Make sure we're not using versions which cause problems.
#
# Avoid versions:
#   3.7.10 - 3.7.13: https://mail.gnome.org/archives/tracker-list/2012-October/msg00028.html
#   3.8.1:           https://mail.gnome.org/archives/tracker-list/2013-November/msg00021.html
#   3.8.4.1:         https://mail.gnome.org/archives/tracker-list/2014-April/msg00001.html
#
AC_MSG_CHECKING(whether SQLite3 version is safe)

PKG_CHECK_EXISTS([sqlite3 <= 3.7.9], sqlite_safe=yes, sqlite_safe=no)
if test "x$sqlite_safe" != "xyes"; then
   PKG_CHECK_EXISTS([sqlite3 >= 3.7.14], sqlite_safe=yes, sqlite_safe=no)
fi

if test "x$sqlite_safe" = "xyes"; then
   PKG_CHECK_EXISTS([sqlite3 = 3.8.1], sqlite_safe=no, sqlite_safe=yes)
fi

if test "x$sqlite_safe" = "xyes"; then
   PKG_CHECK_EXISTS([sqlite3 = 3.8.4.2], sqlite_safe=no, sqlite_safe=yes)
fi


if test "x$sqlite_safe" != "xyes"; then
   AC_MSG_RESULT(no)
else
   AC_MSG_RESULT(yes)
fi

##################################################################
# Check for libtracker-common, make sure libstemmer exists
##################################################################

AC_ARG_ENABLE([libstemmer],
              AS_HELP_STRING([--enable-libstemmer],
                             [enable the stemming while indexing [[default=auto]]]),
              [enable_libstemmer=$enableval],
              [enable_libstemmer=auto])

if test "x$enable_libstemmer" != "xno" ; then
   AC_CHECK_LIB([stemmer], [sb_stemmer_new], [have_libstemmer=yes], [have_libstemmer=no])

   if test "x$have_libstemmer" = "xyes"; then
      AC_DEFINE(HAVE_LIBSTEMMER, [], [Define if we have libstemmer])
      LIBTRACKER_COMMON_LIBS="$LIBTRACKER_COMMON_LIBS -lstemmer"
   fi
else
   have_libstemmer="no  (disabled)"
fi

if test "x$enable_libstemmer" = "xyes"; then
   if test "x$have_libstemmer" != "xyes"; then
      AC_MSG_ERROR([Could not find libstemmer.])
   fi
fi

AM_CONDITIONAL(HAVE_LIBSTEMMER, test "x$have_libstemmer" = "xyes")

##################################################################
# Check for tracker-fts, allow disabling FTS support
##################################################################

AC_ARG_ENABLE([tracker-fts],
              AS_HELP_STRING([--enable-tracker-fts],
                             [enable the tracker FTS [[default=auto]]]),
              [enable_tracker_fts=$enableval],
              [enable_tracker_fts=auto])

if test "x$enable_tracker_fts" = "xno" ; then
   have_tracker_fts="no  (disabled)"
else
   have_tracker_fts="yes"
fi

if test "x$have_tracker_fts" = "xyes"; then
   AC_DEFINE(HAVE_TRACKER_FTS, [1], [Define to 1 if tracker FTS is compiled])

   AX_SQLITE_BUILTIN_FTS5
   if test "x$ax_cv_sqlite_builtin_fts5" = "xyes" ; then
      have_builtin_fts5="yes"
      AC_DEFINE(HAVE_BUILTIN_FTS, [], [Defined if Sqlite has FTS5 compiled in])
   else
      have_builtin_fts5="no"

      # Sqlite 3.20 broke compatibility of the fts5 module both ways, demand
      # that it's builtin into sqlite from that version on.
      PKG_CHECK_EXISTS([sqlite3 >= 3.20.0], sqlite_builtin_fts5_forced=yes, sqlite_builtin_fts5_forced=no)
      if test "x$sqlite_builtin_fts5_forced" == "xyes"; then
        AC_MSG_ERROR([sqlite3 >= 3.20.0 must be compiled with --enable-fts5])
      fi

      # Make sure SQLite has extension loading enabled
      AX_SQLITE_AUTO_EXTENSION
      if test "x$ax_cv_sqlite_auto_extension" != "xyes"; then
        AC_MSG_ERROR([sqlite3 cannot load extensions])
      fi
   fi
else
   AC_DEFINE(HAVE_TRACKER_FTS, [0], [Define to 0 if tracker FTS is not compiled])
   have_builtin_fts5="disabled"
fi

AM_CONDITIONAL(HAVE_BUILTIN_FTS, test "$have_builtin_fts5" = "yes")
AM_CONDITIONAL(HAVE_TRACKER_FTS, test "$have_tracker_fts" = "yes")

####################################################################
# bash-completion
####################################################################

AC_ARG_WITH([bash-completion-dir],
    AS_HELP_STRING([--with-bash-completion-dir[=PATH]],
        [Install the bash auto-completion script in this directory. @<:@default=yes@:>@]),
    [],
    [with_bash_completion_dir=yes])

if test "x$with_bash_completion_dir" = "xyes"; then
   PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0],
       [BASH_COMPLETION_DIR=$($PKG_CONFIG --variable=completionsdir bash-completion)],
       [BASH_COMPLETION_DIR="${datadir}/bash-completion/completions"])
elif test "x$with_bash_completion_dir" != "xno"; then
   # Set with_bash_completions_dir to avoid printing path twice below
   BASH_COMPLETION_DIR="$with_bash_completion_dir"
   with_bash_completion_dir="yes"
fi

AC_SUBST([BASH_COMPLETION_DIR])
AM_CONDITIONAL([WITH_BASH_COMPLETION],[test "x$with_bash_completion_dir" != "xno"])

####################################################################
# Check for D-Bus requirements
####################################################################

# Check we are not using a different D-Bus services dir
AC_ARG_WITH([session_bus_services_dir],
            AS_HELP_STRING([--with-session-bus-services-dir],
                           [path to DBus services directory]),
            [dbus_services_dir=$withval],
            [dbus_services_dir=$datadir/dbus-1/services])

DBUS_SERVICES_DIR="$dbus_services_dir"
AC_SUBST(DBUS_SERVICES_DIR)

####################################################################
# GLib/GIO utilities
####################################################################

GLIB_MKENUMS=`$PKG_CONFIG glib-2.0 --variable=glib_mkenums`
AC_SUBST(GLIB_MKENUMS)

GDBUS_CODEGEN=`$PKG_CONFIG --variable=gdbus_codegen gio-2.0`
AC_SUBST(GDBUS_CODEGEN)

GLIB_COMPILE_RESOURCES=`$PKG_CONFIG --variable glib_compile_resources gio-2.0`
AC_SUBST(GLIB_COMPILE_RESOURCES)

####################################################################
# Should we build GLib based unit tests
####################################################################

AC_ARG_ENABLE(unit_tests,
              AS_HELP_STRING([--enable-unit-tests],
                             [enable unit tests [[default=yes]]]),
              [enable_unit_tests=$enableval],
              [enable_unit_tests=yes])

if test "x$enable_unit_tests" != "xno" ; then
   have_unit_tests=yes

   AC_DEFINE(HAVE_UNIT_TEST, 1, [Define if we have GLib unit test framework])

   AC_PATH_PROG(gtester, gtester)
   if test -z $gtester; then
      AC_MSG_ERROR([Could not find 'gtester'])
   fi
else
   have_unit_tests=no
fi

AM_CONDITIONAL(HAVE_UNIT_TESTS, test "x$have_unit_tests" = "xyes")

##################################################################
# Check for libtracker-common: upower for battery/power support
##################################################################

AC_ARG_ENABLE(upower,
              AS_HELP_STRING([--disable-upower],
                             [disable UPower support for AC power detection [[default=auto]]]),
              [enable_upower=$enableval],
              [enable_upower=auto])

AS_IF([test "x$enable_upower" != "xno"], [
   PKG_CHECK_MODULES(UPOWER,
                     [upower-glib >= $UPOWER_REQUIRED],
                     [have_upower=yes],
                     [have_upower=no])

   TRACKER_MINER_FS_CFLAGS="$TRACKER_MINER_FS_CFLAGS $UPOWER_CFLAGS"
   TRACKER_MINER_FS_LIBS="$TRACKER_MINER_FS_LIBS $UPOWER_LIBS"
   save_LIBS=$LIBS
   LIBS=$UPOWER_LIBS
   AC_CHECK_FUNCS(up_client_get_on_low_battery)
   LIBS=$save_LIBS

   if test "x$have_upower" = "xyes"; then
      AC_DEFINE(HAVE_UPOWER, [], [Define if we have UPOWER])
   fi
], [
   have_upower="no  (disabled)"
])

if test "x$enable_upower" = "xyes"; then
   if test "x$have_upower" != "xyes"; then
      AC_MSG_ERROR([Could not find upower >= $UPOWER_REQUIRED.])
   fi
fi

AM_CONDITIONAL(HAVE_UPOWER, test "x$have_upower" = "xyes")

##################################################################
# Check for libtracker-data and libtracker-fts: Unicode support
#
# By default, AUTO with this order of preference:
#  1)  libunistring
#  2)  libicu
##################################################################

# Check for libunistring...
AC_CHECK_HEADER(uniwbrk.h,
                AC_CHECK_LIB([unistring],
                             [u8_wordbreaks],
                             [have_libunistring=yes],
                             [have_libunistring=no]))
LIBUNISTRING_CFLAGS=""
LIBUNISTRING_LIBS="-lunistring"

# Check for libicu...
PKG_CHECK_MODULES(LIBICU,
                  [icu-i18n >= $LIBICU_REQUIRED icu-uc >= $LIBICU_REQUIRED],
                  [have_libicu=yes],
                  [have_libicu=no])

# Configure option
AC_ARG_WITH([unicode-support],
            AS_HELP_STRING([--with-unicode-support],
                           [Unicode support library? (libunistring|libicu) [[default=auto]]]),
            [with_unicode_support=$withval],
            [with_unicode_support=auto])

# If auto, decide ourselves
if test "x$with_unicode_support" = "xauto"; then
   if test "x$have_libicu" = "xyes"; then
      with_unicode_support=libicu
   else
      if test "x$have_libunistring" = "xyes"; then
         with_unicode_support=libunistring
      else
         AC_MSG_ERROR([Could not find either libunistring or libicu])
      fi
   fi
fi

UNICODE_SUPPORT_CFLAGS=
UNICODE_SUPPORT_LIBS=

case "x$with_unicode_support" in
     # Use libunistring
     "xlibunistring")
        UNICODE_SUPPORT_CFLAGS=$LIBUNISTRING_CFLAGS
        UNICODE_SUPPORT_LIBS=$LIBUNISTRING_LIBS
        if test "x$have_libunistring" = "xyes"; then
           AC_DEFINE(HAVE_LIBUNISTRING, [1], [libunistring Unicode support library])
        else
           AC_MSG_ERROR([Could not find libunistring])
        fi
     ;;
     # Use libicu
     "xlibicu")
        UNICODE_SUPPORT_CFLAGS=$LIBICU_CFLAGS
        UNICODE_SUPPORT_LIBS=$LIBICU_LIBS
        if test "x$have_libicu" = "xyes"; then
          AC_DEFINE(HAVE_LIBICU, [1], [libicu Unicode support library])
        else
          AC_MSG_ERROR([Could not find libicu])
        fi
     ;;
     # Invalid option value
     *) AC_MSG_ERROR([Wrong value for --with-unicode-support: $with_unicode_support]) ;;
esac

AM_CONDITIONAL(HAVE_LIBICU, test "x$have_libicu" = "xyes")

# Add to libtracker-common
LIBTRACKER_COMMON_CFLAGS="$LIBTRACKER_COMMON_CFLAGS $UNICODE_SUPPORT_CFLAGS"
LIBTRACKER_COMMON_LIBS="$LIBTRACKER_COMMON_LIBS $UNICODE_SUPPORT_LIBS"

# Add to libtracker-fts
LIBTRACKER_FTS_CFLAGS="$LIBTRACKER_FTS_CFLAGS $UNICODE_SUPPORT_CFLAGS"
LIBTRACKER_FTS_LIBS="$LIBTRACKER_FTS_LIBS $UNICODE_SUPPORT_LIBS"

# Add to libtracker-data
LIBTRACKER_DATA_CFLAGS="$LIBTRACKER_DATA_CFLAGS $UNICODE_SUPPORT_CFLAGS"
LIBTRACKER_DATA_LIBS="$LIBTRACKER_DATA_LIBS $UNICODE_SUPPORT_LIBS"

# Add to libtracker-extract
LIBTRACKER_EXTRACT_CFLAGS="$LIBTRACKER_EXTRACT_CFLAGS $UNICODE_SUPPORT_CFLAGS"
LIBTRACKER_EXTRACT_LIBS="$LIBTRACKER_EXTRACT_LIBS $UNICODE_SUPPORT_LIBS"

AM_CONDITIONAL(BUILD_LIBUNISTRING_PARSER, test "x$with_unicode_support" = "xlibunistring")
AM_CONDITIONAL(BUILD_LIBICU_PARSER, test "x$with_unicode_support" = "xlibicu")

##################################################################
# Check for libtracker-miner: network-manager for web miners
##################################################################

AC_ARG_ENABLE(network_manager,
              AS_HELP_STRING([--enable-network-manager],
                             [enable NetworkManager support to detect network status [[default=auto]]]),
              [enable_network_manager=$enableval],
              [enable_network_manager=auto])

if test "x$enable_network_manager" != "xno"; then

   PKG_CHECK_MODULES(NETWORK_MANAGER,
                     [libnm],
                     [have_network_manager=yes],
                     [have_network_manager=no])

   LIBTRACKER_MINER_CFLAGS="$LIBTRACKER_MINER_CFLAGS $NETWORK_MANAGER_CFLAGS"
   LIBTRACKER_MINER_LIBS="$LIBTRACKER_MINER_LIBS $NETWORK_MANAGER_LIBS"

   if test "x$have_network_manager" = "xyes"; then
      AC_DEFINE(HAVE_NETWORK_MANAGER, [], [Define if we have NetworkManager for network status detection])
   fi
fi

if test "x$enable_network_manager" = "xyes"; then
   if test "x$have_network_manager" != "xyes"; then
      AC_MSG_ERROR([Could not find libnm.])
   fi
fi

AM_CONDITIONAL(HAVE_NETWORK_MANAGER, test "x$have_network_manager" = "xyes")

##################################################################
# Check for tracker-resdump
##################################################################

AM_CONDITIONAL(HAVE_TRACKER_RESDUMP, test "$have_tracker_resdump" = "yes")

##################################################################
# Write generated files
##################################################################

AC_CONFIG_FILES([
	docs/Makefile
	docs/design/Makefile
	docs/manpages/Makefile
	docs/reference/Makefile
	docs/reference/libtracker-control/Makefile
	docs/reference/libtracker-control/version.xml
	docs/reference/libtracker-sparql/Makefile
	docs/reference/libtracker-sparql/version.xml
	docs/reference/libtracker-miner/Makefile
	docs/reference/libtracker-miner/version.xml
	docs/reference/ontology/Makefile
	docs/reference/ontology/version.xml
	docs/tools/Makefile
	Makefile
	po/Makefile.in
	src/gvdb/Makefile
	src/libtracker-common/Makefile
	src/libtracker-common/stop-words/Makefile
	src/libtracker-sparql/Makefile
	src/libtracker-sparql/tracker-sparql.pc
	src/libtracker-bus/Makefile
	src/libtracker-control/Makefile
	src/libtracker-control/tracker-control.pc
	src/libtracker-data/Makefile
	src/libtracker-direct/Makefile
	src/libtracker-remote/Makefile
	src/libtracker-sparql-backend/Makefile
	src/libtracker-fts/Makefile
	src/libtracker-miner/Makefile
	src/libtracker-miner/tracker-miner.pc
	src/Makefile
	src/ontologies/Makefile
	src/tracker-store/Makefile
	src/tracker/Makefile
	tests/common/Makefile
        tests/gvdb/Makefile
	tests/libtracker-common/Makefile
	tests/libtracker-data/Makefile
	tests/libtracker-data/aggregates/Makefile
	tests/libtracker-data/algebra/Makefile
	tests/libtracker-data/anon/Makefile
	tests/libtracker-data/ask/Makefile
	tests/libtracker-data/basic/Makefile
	tests/libtracker-data/bind/Makefile
	tests/libtracker-data/bnode-coreference/Makefile
	tests/libtracker-data/bound/Makefile
	tests/libtracker-data/datetime/Makefile
	tests/libtracker-data/expr-ops/Makefile
	tests/libtracker-data/functions/Makefile
	tests/libtracker-data/graph/Makefile
	tests/libtracker-data/nie/Makefile
	tests/libtracker-data/nmo/Makefile
	tests/libtracker-data/mixed-cardinality/Makefile
	tests/libtracker-data/optional/Makefile
	tests/libtracker-data/regex/Makefile
	tests/libtracker-data/sort/Makefile
	tests/libtracker-data/subqueries/Makefile
	tests/libtracker-data/error/Makefile
	tests/libtracker-data/backup/Makefile
	tests/libtracker-data/turtle/Makefile
	tests/libtracker-data/update/Makefile
	tests/libtracker-miner/Makefile
	tests/libtracker-fts/Makefile
	tests/libtracker-fts/limits/Makefile
	tests/libtracker-fts/prefix/Makefile
	tests/libtracker-sparql/Makefile
	tests/functional-tests/Makefile
	tests/functional-tests/ipc/Makefile
	tests/functional-tests/common/Makefile
	tests/functional-tests/common/utils/configuration.py
	tests/functional-tests/common/utils/Makefile
	tests/functional-tests/unittest2/Makefile
	tests/functional-tests/test-ontologies/Makefile
	tests/functional-tests/test-ontologies/basic/Makefile
	tests/functional-tests/test-ontologies/property-range-int-to-string/Makefile
	tests/functional-tests/test-ontologies/property-range-string-to-date/Makefile
	tests/functional-tests/test-ontologies/cardinality/Makefile
	tests/functional-tests/test-ontologies/notify/Makefile
	tests/functional-tests/test-ontologies/indexed/Makefile
	tests/functional-tests/test-ontologies/add-class/Makefile
	tests/functional-tests/test-ontologies/add-prop/Makefile
	tests/functional-tests/test-ontologies/add-domainIndex/Makefile
	tests/functional-tests/test-ontologies/superclass-remove/Makefile
	tests/functional-tests/test-ontologies/property-promotion/Makefile
	tests/functional-tests/test-ontologies/basic-future/Makefile
	tests/functional-tests/test-apps-data/Makefile
	tests/functional-tests/ttl/Makefile
	tests/Makefile
	tests/tracker-steroids/Makefile
	utils/Makefile
	utils/ontology/Makefile
	utils/data-generators/Makefile
	utils/data-generators/cc/Makefile
	utils/mtp/Makefile
	utils/sandbox/Makefile
	utils/tracker-resdump/Makefile
	examples/Makefile
	examples/libtracker-miner/Makefile
	examples/libtracker-sparql/Makefile
	examples/rss-reader/Makefile
])

AC_OUTPUT

echo "
Build Configuration:

	Prefix:                                 ${prefix}
	Source code location:                   ${srcdir}
	Compiler:                               ${CC}
	Compiler Warnings:                      $enable_compile_warnings
          C    Flags:                            $warning_cflags
          VALA Flags:                            $warning_valacflags

	Win32:                                  $native_win32

	Enable gtk doc (for documentation):     $enable_gtk_doc

	Enable functional tests:                $enable_functional_tests
	Enable unit tests:                      $have_unit_tests

Feature Support:

	Support for UPower:                     $have_upower
	Support for network status detection:   $have_network_manager
	Unicode support library:                $with_unicode_support

	Build with Journal support:             $have_tracker_journal
	Build with SQLite FTS support:          $have_tracker_fts (built-in FTS: $have_builtin_fts5)
	Build with Stemming support:            $have_libstemmer

	Bash completion support:                $with_bash_completion_dir ($BASH_COMPLETION_DIR)

Frameworks / Options:

	Support Guaranteed Metadata             $enable_guarantee_metadata (e.g. guess nie:title from files)

"

# Any warnings we need to issue...
if test "x$sqlite_safe" = "xno"; then
sqlite_version=$($PKG_CONFIG --modversion sqlite3)
echo "
WARNING:
        SQLite3 version $sqlite_version is not safe. For details see:
        3.7.10 - 3.7.13: https://mail.gnome.org/archives/tracker-list/2012-October/msg00028.html
        3.8.1:           https://mail.gnome.org/archives/tracker-list/2013-November/msg00021.html
        3.8.4.2:         https://mail.gnome.org/archives/tracker-list/2014-April/msg00001.html


"
fi
