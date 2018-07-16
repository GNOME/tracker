SUBDIRS = \
	ipc \
	common \
	ttl \
	unittest2 \
	test-ontologies

configdir = $(datadir)/tracker-tests

config_DATA = \
	configuration.json \
	trackertest

config_SCRIPTS = \
	__init__.py \
	$(slow_tests) \
	$(standard_tests)

standard_tests = \
	01-insertion.py \
	02-sparql-bugs.py
if HAVE_TRACKER_FTS
standard_tests += 03-fts-functions.py
endif
standard_tests += \
	04-group-concat.py \
	05-coalesce.py \
	06-distance.py \
	07-graph.py \
	08-unique-insertions.py \
	09-concurrent-query.py \
	14-signals.py \
	15-statistics.py \
	16-collation.py \
	17-ontology-changes.py

slow_tests = \
	10-sqlite-misused.py \
	11-sqlite-batch-misused.py \
	12-transactions.py \
	13-threaded-store.py

10-sqlite-misused.py: ttl/gen-test-data.stamp
11-sqlite-misused-batch.py: ttl/gen-test-data.stamp
13-busy-store.py: ttl/gen-test-data.stamp

ttl/gen-test-data.stamp:
	cd ttl && $(MAKE) $(AM_MAKEFLAGS)

TEST_RUNNER = $(top_srcdir)/tests/functional-tests/test-runner.sh

functional-test: ${standard_tests}
	for test in ${standard_tests} ; do \
		export TRACKER_DB_ONTOLOGIES_DIR=$(top_srcdir)/src/ontologies/nepomuk; \
		export TRACKER_FUNCTIONAL_TEST_BUILD_DIR=$(top_builddir) ; \
		export TRACKER_FUNCTIONAL_TEST_CONFIG=$(top_builddir)/tests/functional-tests/configuration.json ; \
		export TRACKER_LANGUAGE_STOP_WORDS_DIR=$(top_srcdir)/src/libtracker-common/stop-words ; \
		export TRACKER_TEST_DOMAIN_ONTOLOGY_RULE=$(top_srcdir)/src/tracker-store/default.rule ; \
		$(TEST_RUNNER) python $(top_srcdir)/tests/functional-tests/$$test; \
	done

functional-test-slow: ${slow_tests}
	@for test in ${slow_tests} ; do \
		export TRACKER_DB_ONTOLOGIES_DIR=$(top_srcdir)/src/ontologies/nepomuk; \
		export TRACKER_FUNCTIONAL_TEST_BUILD_DIR=$(top_builddir) ; \
		export TRACKER_FUNCTIONAL_TEST_CONFIG=$(top_builddir)/tests/functional-tests/configuration.json ; \
		export TRACKER_LANGUAGE_STOP_WORDS_DIR=$(top_srcdir)/src/libtracker-common/stop-words ; \
		export TRACKER_TEST_DOMAIN_ONTOLOGY_RULE=$(top_srcdir)/src/tracker-store/default.rule ; \
		$(TEST_RUNNER) python $(top_srcdir)/tests/functional-tests/$$test; \
	done

EXTRA_DIST = \
	$(config_SCRIPTS) \
	$(config_DATA) \
	configuration.json.in \
	test-runner.sh \
	meson.build
