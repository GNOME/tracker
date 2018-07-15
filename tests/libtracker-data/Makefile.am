include $(top_srcdir)/Makefile.decl

SUBDIRS =                                              \
	aggregates                                     \
	algebra                                        \
	anon                                           \
	ask                                            \
	basic                                          \
	bind                                           \
	bnode-coreference                              \
	bound                                          \
	datetime                                       \
	expr-ops                                       \
	functions                                      \
	graph                                          \
	nie                                            \
	nmo                                            \
	mixed-cardinality                              \
	optional                                       \
	regex                                          \
	sort                                           \
	subqueries                                     \
	error                                          \
	backup                                         \
	update                                         \
	turtle

noinst_PROGRAMS += $(test_programs)

test_programs = \
	tracker-sparql                                 \
	tracker-sparql-blank                           \
	tracker-ontology                               \
	tracker-backup                                 \
	tracker-crc32-test			       \
	tracker-ontology-change                        \
	tracker-db-journal

AM_CPPFLAGS =                                          \
	$(BUILD_CFLAGS)                                \
	-DTOP_SRCDIR=\"$(abs_top_srcdir)\"             \
	-DTOP_BUILDDIR=\"$(abs_top_builddir)\"         \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	-I$(top_srcdir)/tests/common                   \
	$(LIBTRACKER_DATA_CFLAGS)

LDADD =                                                \
	$(top_builddir)/src/libtracker-common/libtracker-common.la \
	$(top_builddir)/src/libtracker-data/libtracker-data.la \
	$(top_builddir)/src/libtracker-sparql-backend/libtracker-sparql-@TRACKER_API_VERSION@.la \
	$(top_builddir)/tests/common/libtracker-testcommon.la \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_DATA_LIBS)

tracker_sparql_SOURCES = tracker-sparql-test.c
tracker_sparql_blank_SOURCES = tracker-sparql-blank-test.c
tracker_ontology_SOURCES = tracker-ontology-test.c
tracker_ontology_change_SOURCES = tracker-ontology-change-test.c
tracker_backup_SOURCES = tracker-backup-test.c
tracker_crc32_test_SOURCES = tracker-crc32-test.c
tracker_db_journal_SOURCES = tracker-db-journal-test.c

EXTRA_DIST += \
	dawg-testcases                                 \
	change/source/99-example.ontology.v1           \
	change/source/99-example.ontology.v2           \
	change/source/99-example.ontology.v3           \
	change/source/99-example.ontology.v4           \
	change/source/99-example.ontology.v5           \
	change/source/99-example.ontology.v6           \
	change/source/99-example.ontology.v7           \
	change/test-1.out                              \
	change/test-1.rq                               \
	change/test-2.out                              \
	change/test-2.rq                               \
	change/test-3.out                              \
	change/test-3.rq                               \
	change/test-4.rq                               \
	change/test-4.out                              \
	change/test-5.out                              \
	change/test-5.rq                               \
	change/change-test-1.rq                        \
	change/change-test-1.out                       \
	change/change-test-2.rq                        \
	change/change-test-2.out                       \
	change/change-test-3.rq                        \
	change/change-test-3.out                       \
	change/updates/99-example.queries.v2           \
	change/updates/99-example.queries.v3           \
	change/updates/99-example.queries.v4           \
	change/updates/99-example.queries.v5           \
	change/updates/99-example.queries.v6           \
	change/updates/99-example.queries.v7           \
	meson.build

clean-local:
	rm -rf $(abs_builddir)/test-cache
