manpages = [
    'endpoint',
    'export',
    'import',
    'info',
    'sparql',
    'sql'
]

foreach m : manpages
  manpage_src = 'tracker-@0@.1.txt'.format(m)
  manpage_xml = 'tracker-@0@.1.xml'.format(m)
  manpage = 'tracker@0@-@1@.1'.format(tracker_api_major, m)

  xml = custom_target(manpage_xml,
    command: [asciidoc,
              '--attribute=author=The Tracker developers',
              '--attribute=manversion=@0@'.format(meson.project_version()),
              '--attribute=manmanual=Tracker manual',
              '--backend', 'docbook',
              '--doctype', 'manpage',
              '--out-file', '@OUTPUT@', '@INPUT@'],
    input: manpage_src,
    output: manpage_xml
  )

  custom_target(manpage,
    command: [xsltproc,
              '--stringparam', 'man.authors.section.enabled', '0',
              '/etc/asciidoc/docbook-xsl/manpage.xsl', '@INPUT@'],
    capture: true,
    input: xml,
    output: manpage,
    install: true,
    install_dir: get_option('mandir') / 'man1',
  )
endforeach
