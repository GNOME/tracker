include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS += $(test_programs)

test_programs = \
	tracker-resource-test                          \
	tracker-sparql-test

AM_CPPFLAGS =                                          \
	$(BUILD_CFLAGS)                                \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	-I$(top_srcdir)/tests/common                   \
	-DTEST_DOMAIN_ONTOLOGY_RULE=\""$(abs_top_srcdir)/src/tracker-store/default.rule"\" \
	-DTEST_ONTOLOGIES_DIR=\""$(abs_top_srcdir)/src/ontologies/nepomuk"\" \
	$(LIBTRACKER_SPARQL_CFLAGS)

LDADD =                                                \
	$(top_builddir)/src/libtracker-sparql-backend/libtracker-sparql-@TRACKER_API_VERSION@.la \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_SPARQL_LIBS)

tracker_resource_test_SOURCES = tracker-resource-test.c

tracker_sparql_test_SOURCES = tracker-sparql-test.c

tracker_gb_737023_test_SOURCES = tracker-gb-737023-test.c

EXTRA_DIST += meson.build
