if sqlite3_has_builtin_fts5
    libtracker_fts_fts5 = []
else
    libtracker_fts_fts5 = files('fts5.c')
endif

libtracker_fts_dependencies = [sqlite]

libtracker_fts = static_library('tracker-fts',
    'tracker-fts.c',
    'tracker-fts-config.c',
    'tracker-fts-tokenizer.c',
    libtracker_fts_fts5,
    tracker_sparql_generated_header,
    dependencies: [tracker_common_dep],
    c_args: tracker_c_args
)

tracker_fts_dep = declare_dependency(
    # We need link_whole instead of link_with here so that sqlite3_fts5_init()
    # doesn't get discarded.
    link_whole: libtracker_fts,
    dependencies: libtracker_fts_dependencies
)
