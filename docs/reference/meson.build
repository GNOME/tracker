glib_prefix = dependency('glib-2.0').get_pkgconfig_variable('prefix')
glib_docpath = join_paths(glib_prefix, 'share', 'gtk-doc', 'html')

docpath = join_paths(datadir, 'gtk-doc')

fixxref_args = [
    '--html-dir=@0@'.format(docpath),
    '--extra-dir=@0@'.format(join_paths(glib_docpath, 'glib')),
    '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gobject')),
    '--extra-dir=@0@'.format(join_paths(glib_docpath, 'gio')),
    '--extra-dir=@0@'.format(join_paths(docpath, 'libtracker-sparql')),
]

icon_images = files(
  'images/icon-fulltextindexed.svg'
)

subdir('libtracker-sparql')

subdir('ontology')
