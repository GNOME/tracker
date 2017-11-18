lib_LTLIBRARIES = libtracker-sparql-@TRACKER_API_VERSION@.la

AM_VALAFLAGS =                                         \
	--pkg gio-2.0                                  \
	$(BUILD_VALAFLAGS)                             \
	$(top_srcdir)/src/libtracker-sparql/tracker-sparql-$(TRACKER_API_VERSION).vapi \
	$(top_srcdir)/src/libtracker-bus/tracker-bus.vapi \
	$(top_srcdir)/src/libtracker-data/libtracker-data.vapi \
	$(top_srcdir)/src/libtracker-direct/tracker-direct.vapi \
	$(top_srcdir)/src/libtracker-remote/tracker-remote.vapi \
	$(top_srcdir)/src/libtracker-common/libtracker-common.vapi

AM_CPPFLAGS =                                          \
	$(BUILD_VALACFLAGS)                            \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	$(LIBTRACKER_REMOTE_CFLAGS)                    \
	$(LIBTRACKER_SPARQL_CFLAGS)

libtracker_sparql_@TRACKER_API_VERSION@_la_SOURCES =   \
	tracker-backend.vala

libtracker_sparql_@TRACKER_API_VERSION@_la_LIBADD =    \
	$(top_builddir)/src/libtracker-sparql/libtracker-sparql-intermediate-vala.la \
	$(top_builddir)/src/libtracker-sparql/libtracker-sparql-intermediate-c.la \
	$(top_builddir)/src/libtracker-bus/libtracker-bus.la \
	$(top_builddir)/src/libtracker-direct/libtracker-direct.la \
	$(top_builddir)/src/libtracker-remote/libtracker-remote.la \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_REMOTE_LIBS)                      \
	$(LIBTRACKER_SPARQL_LIBS)

libtracker_sparql_@TRACKER_API_VERSION@_la_LDFLAGS =   \
	$(DTAGS_LDFLAGS)                               \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-export-symbols-regex '^(tracker_sparql_(connection|cursor|builder|escape|error|value|get)_.*|tracker_namespace_manager_.*|tracker_resource_.*|tracker_notifier_.*)'

EXTRA_DIST = meson.build

# Introspection foo
-include $(INTROSPECTION_MAKEFILE)

if HAVE_INTROSPECTION

c_introspection_files = \
	$(top_builddir)/src/libtracker-sparql/tracker-sparql-enum-types.c \
	$(top_builddir)/src/libtracker-sparql/tracker-sparql-enum-types.h \
	$(top_srcdir)/src/libtracker-sparql/tracker-namespace-manager.c \
	$(top_srcdir)/src/libtracker-sparql/tracker-namespace-manager.h \
	$(top_srcdir)/src/libtracker-sparql/tracker-notifier.c \
	$(top_srcdir)/src/libtracker-sparql/tracker-notifier.h \
	$(top_srcdir)/src/libtracker-sparql/tracker-resource.c \
	$(top_srcdir)/src/libtracker-sparql/tracker-resource.h \
	$(top_srcdir)/src/libtracker-sparql/tracker-ontologies.h \
	$(top_srcdir)/src/libtracker-sparql/tracker-version.c \
	$(top_srcdir)/src/libtracker-sparql/tracker-version.h

INTROSPECTION_SCANNER_ARGS = \
	-I $(top_srcdir)/src \
	-D TRACKER_COMPILATION \
	--identifier-prefix=Tracker \
	--symbol-prefix=tracker \
	--warn-all

Tracker_C-@TRACKER_API_VERSION@.gir: $(INTROSPECTION_SCANNER) Makefile libtracker-sparql-@TRACKER_API_VERSION@.la
Tracker_C_@TRACKER_API_VERSION_UNDERSCORES@_gir_INCLUDES = GObject-2.0 GModule-2.0 Gio-2.0
Tracker_C_@TRACKER_API_VERSION_UNDERSCORES@_gir_CFLAGS = $(INCLUDES)
Tracker_C_@TRACKER_API_VERSION_UNDERSCORES@_gir_NAMESPACE = "Tracker"
Tracker_C_@TRACKER_API_VERSION_UNDERSCORES@_gir_LIBS = libtracker-sparql-@TRACKER_API_VERSION@.la
Tracker_C_@TRACKER_API_VERSION_UNDERSCORES@_gir_FILES = $(c_introspection_files)

INTROSPECTION_GIRS = Tracker_C-$(TRACKER_API_VERSION).gir

noinst_DATA = $(INTROSPECTION_GIRS)

Tracker-@TRACKER_API_VERSION@.gir: $(INTROSPECTION_GIRS) $(top_srcdir)/src/libtracker-sparql/Tracker_Vala-$(TRACKER_API_VERSION).gir
	$(AM_V_GEN) ($(top_srcdir)/utils/g-ir-merge/g-ir-merge --namespace Tracker --nsversion $(TRACKER_API_VERSION) $(INTROSPECTION_GIRS) $(top_srcdir)/src/libtracker-sparql/Tracker_Vala-$(TRACKER_API_VERSION).gir) > $@

GIRS = Tracker-$(TRACKER_API_VERSION).gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(GIRS:.gir=.typelib)

CLEANFILES = $(gir_DATA) $(typelib_DATA) $(INTROSPECTION_GIRS)
endif
