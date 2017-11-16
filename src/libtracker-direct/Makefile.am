noinst_LTLIBRARIES = libtracker-direct.la

AM_CPPFLAGS =                                          \
	$(BUILD_VALACFLAGS)                            \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	-DSHAREDIR=\""$(datadir)"\"                    \
	$(LIBTRACKER_DIRECT_CFLAGS)

libtracker_direct_la_SOURCES =                         \
	tracker-direct.c

libtracker_direct_la_LIBADD =                          \
	$(top_builddir)/src/libtracker-data/libtracker-data.la \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_DIRECT_LIBS)

noinst_HEADERS =                                       \
	tracker-direct.h

EXTRA_DIST = meson.build
