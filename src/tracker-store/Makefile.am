AM_CPPFLAGS =                                          \
	$(BUILD_VALACFLAGS)                            \
	-DSHAREDIR=\""$(datadir)"\"                    \
	-DLOCALEDIR=\""$(localedir)"\"                 \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	$(TRACKER_STORE_CFLAGS)                        \
	-include config.h

libexec_PROGRAMS = tracker-store

tracker_store_SOURCES =                                \
	tracker-backup.vala                            \
	tracker-config.c                               \
	tracker-dbus.vala                              \
	tracker-events.c                               \
	tracker-main.vala                              \
	tracker-resources.vala                         \
	tracker-statistics.vala                        \
	tracker-status.vala                            \
	tracker-steroids.vala                          \
	tracker-store.vala                             \
	tracker-writeback.c

noinst_HEADERS =                                       \
	tracker-config.h                               \
	tracker-events.h                               \
	tracker-store.h                                \
	tracker-writeback.h

BUILT_SOURCES = tracker-store.h

tracker_store_VALAFLAGS = \
	--pkg gio-2.0 \
	--pkg gio-unix-2.0 \
	--pkg posix \
	$(BUILD_VALAFLAGS) \
	$(top_srcdir)/src/libtracker-common/libtracker-common.vapi \
	$(top_srcdir)/src/libtracker-sparql/tracker-sparql-$(TRACKER_API_VERSION).vapi \
	$(top_srcdir)/src/libtracker-data/tracker-sparql-query.vapi \
	$(top_srcdir)/src/libtracker-data/libtracker-data.vapi \
	$(top_srcdir)/src/libtracker-direct/tracker-direct.vapi \
	$(top_srcdir)/src/tracker-store/tracker-config.vapi \
	$(top_srcdir)/src/tracker-store/tracker-events.vapi \
	$(top_srcdir)/src/tracker-store/tracker-locale-change.vapi \
	$(top_srcdir)/src/tracker-store/tracker-writeback.vapi \
	-H tracker-store.h

tracker_store_LDADD =                                  \
	$(top_builddir)/src/libtracker-data/libtracker-data.la \
	$(top_builddir)/src/libtracker-direct/libtracker-direct.la \
	$(top_builddir)/src/libtracker-common/libtracker-common.la \
	$(top_builddir)/src/libtracker-sparql-backend/libtracker-sparql-@TRACKER_API_VERSION@.la \
	$(BUILD_LIBS)                                  \
	$(TRACKER_STORE_LIBS)

EXTRA_DIST = \
	tracker-config.vapi \
	tracker-events.vapi \
	tracker-locale-change.vapi \
	tracker-writeback.vapi


# DBus interfaces (XML files) and services
configdir = $(datadir)/tracker
config_DATA = \
	tracker-backup.xml \
	tracker-resources.xml \
	tracker-statistics.xml \
	tracker-status.xml

%.service: %.service.in
	@sed -e "s|@libexecdir[@]|${libexecdir}|" $< > $@

servicedir = $(DBUS_SERVICES_DIR)
service_DATA = org.freedesktop.Tracker1.service

systemd_userdir = $(prefix)/lib/systemd/user
systemd_user_DATA = tracker-store.service

domain_ontologiesdir = $(datadir)/tracker/domain-ontologies
domain_ontologies_DATA = default.rule

# Desktop files: First we fix the VERSION and libexecdir
%.desktop.in: %.desktop.in.in
	@sed -e "s|@libexecdir[@]|${libexecdir}|" \
	     -e "s|@VERSION[@]|${VERSION}|" $< > $@

autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = tracker-store.desktop

@INTLTOOL_DESKTOP_RULE@

# Configuration / GSettings
gsettings_ENUM_NAMESPACE = org.freedesktop.Tracker
gsettings_ENUM_FILES = $(top_srcdir)/src/libtracker-common/tracker-enums.h

gsettings_SCHEMAS = $(schemalist) $(config)

config = org.freedesktop.Tracker.Store.gschema.xml
schemalist = org.freedesktop.Tracker.gschema.xml

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

# Dist / Clean rules for all data
CLEANFILES = \
	$(autostart_DATA) \
	$(autostart_DATA:.desktop=.desktop.in) \
	$(service_DATA) \
	$(systemd_user_DATA) \
	$(gsettings_SCHEMAS) \
	$(man_MANS)

EXTRA_DIST += \
	$(autostart_DATA:.desktop=.desktop.in.in) \
	$(service_DATA:.service=.service.in) \
	$(systemd_user_DATA:.service=.service.in) \
	$(config_DATA) \
	$(gsettings_SCHEMAS:.xml=.xml.in) \
	$(man_MANS) \
	$(domain_ontologies_DATA) \
	meson.build

