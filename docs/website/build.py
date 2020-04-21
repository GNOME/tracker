#!/usr/bin/env python3
# Tracker website build script.
#
# Copyright 2020, Sam Thursfield <sam@afuera.me.uk>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.


import bs4

import argparse
import contextlib
import logging
import pathlib
import shutil
import subprocess
import sys
import tempfile

log = logging.getLogger('build.py')

output_path = pathlib.Path('public')
website_root = pathlib.Path(__file__).parent
docs_root = website_root.parent
source_root = docs_root.parent

asciidoc = shutil.which('asciidoc')
mkdocs = shutil.which('mkdocs')
xmlto = shutil.which('xmlto')


def argument_parser():
    parser = argparse.ArgumentParser(
        description="Tracker website build script")
    parser.add_argument('--debug', dest='debug', action='store_true',
                        help="Enable detailed logging to stderr")
    parser.add_argument('--api-docs', required=True, metavar='PATH',
                        help="Path that contains API documentation. Usually "
                             "$prefix/share/gtk-doc/html/")
    parser.add_argument('--tracker-commit', required=True, metavar='COMMIT',
                        help="Commit ID of tracker.git repo used to build")
    parser.add_argument('--man-pages', nargs='+', required=True,
                        help="List of Asciidoc manual page source")
    return parser


def apidocs_header(tracker_commit):
    return f"""<!-- Inserted by {__file__} -->
    <div class="warning">
        <p>This is a documentation preview for the next version of Tracker,
           generated from <a href="https://gitlab.gnome.org/GNOME/tracker/commit/{tracker_commit}/">tracker.git commit {tracker_commit[:7]}</a>.</p>
        <p>See the <a href="../..">Tracker website</a> for more documentation.</p>
    </div>"""


def add_apidocs_header(text, filename):
    """Add a header to the documentation preview files."""

    # We insert the header before any of these
    markers = [
        '<div class="book">',
        '<div class="chapter">',
        '<div class="index">',
        '<div class="glossary">',
        '<div class="part">',
        '<div class="refentry">',
        '<div class="section">',
    ]

    with open(filename, encoding='utf8') as f_in:
        with tempfile.NamedTemporaryFile(delete=False, mode='w', encoding='utf8') as f_out:
            for line in f_in:
                for marker in markers:
                    if line.find(marker) != -1:
                        f_out.write(text)
                f_out.write(line)
    shutil.move(f_out.name, filename)


class Manpages():
    def run(self, command):
        command = [str(c) for c in command]
        log.debug("Running: %s", ' '.join(command))
        subprocess.run(command, check=True)

    def generate_manpage_xml(self, in_path, out_path):
        """Generate a docbook XML file for an Asciidoc manpage source file."""
        self.run([asciidoc, '--backend', 'docbook', '--doctype', 'manpage',
                 '--out-file', out_path, in_path])

    def generate_manpage_html(self, in_path, out_path):
        """Generate a HTML page from a docbook XML file"""
        self.run([xmlto, 'xhtml-nochunks', '-o', out_path, in_path])

    def generate_toplevel_markdown(self, in_path, out_path, html_files):
        """Generate the master commandline.md page."""

        includes = []
        for path in sorted(html_files):
            parser = bs4.BeautifulSoup(path.read_text(), 'html.parser')

            title = parser.title.text
            if title and title.startswith('tracker-'):
                title = 'tracker ' + title[8:]

            body = parser.body.contents[0]

            includes.append("## %s\n\n%s\n---\n" % (title, body))

        text = in_path.read_text()
        text = text.format(
            includes='\n'.join(includes)
        )
        out_path.write_text(text)


@contextlib.contextmanager
def tmpdir():
    path = pathlib.Path(tempfile.mkdtemp())
    log.debug("Created temporary directory %s", path)
    try:
        yield path
    finally:
        log.debug("Removed temporary directory %s", path)
        shutil.rmtree(path)


def main():
    args = argument_parser().parse_args()

    if args.debug:
        logging.basicConfig(stream=sys.stderr, level=logging.DEBUG)
    else:
        logging.basicConfig(stream=sys.stderr, level=logging.INFO)

    if output_path.exists():
        raise RuntimeError(f"Output path '{output_path}' already exists.")

    if asciidoc is None:
        raise RuntimeError("The 'asciidoc' tool is required.")
    if xmlto is None:
        raise RuntimeError("The 'xmlto' tool is required.")

    log.info("Generating online man pages")
    with tmpdir() as workdir:
        manpages = Manpages()

        htmldir = website_root.joinpath('docs')
        htmlfiles = []

        for man_page in args.man_pages:
            asciidocpath = pathlib.Path(man_page)
            xmlpath = workdir.joinpath(asciidocpath.stem + '.xml')

            manpages.generate_manpage_xml(asciidocpath, xmlpath)
            manpages.generate_manpage_html(xmlpath, htmldir)

            htmlpath = htmldir.joinpath(xmlpath.with_suffix('.html').name)
            htmlfiles.append(htmlpath)

        template_in = website_root.joinpath('docs/commandline.md.in')
        template_out = website_root.joinpath('docs/commandline.md')

        manpages.generate_toplevel_markdown(template_in, template_out, htmlfiles)

    #shutil.copy('/usr/share/asciidoc/stylesheets/docbook-xsl.css', manpage_output_path)

    log.info("Building website")
    mkdocs_config = docs_root.joinpath('mkdocs.yml')
    subprocess.run([mkdocs, 'build', '--config-file', mkdocs_config,
                    '--site-dir', output_path.absolute()],
                   check=True)

    apidocs_src = pathlib.Path(args.api_docs)

    log.info("Copying API reference documentation from %s", apidocs_src)
    apidocs_dest = output_path.joinpath('docs/api-preview')
    apidocs_dest.mkdir(parents=True)
    for name in ['libtracker-sparql3', 'tracker3-nepomuk']:
        src = apidocs_src.joinpath(name)
        dest  = apidocs_dest.joinpath(name)
        if not src.exists():
            raise RuntimeError("Expected path {} doesn't exist.".format(src))
        log.info("  - Copying %s to %s", src, dest)
        shutil.copytree(src, dest)

    log.info("Adding preview header to API reference documentation")
    text = apidocs_header(args.tracker_commit)
    for filename in apidocs_dest.rglob('*.html'):
        add_apidocs_header(text, filename)

    log.info("Documentation available in public/ directory.")


try:
    main()
except (RuntimeError, subprocess.CalledProcessError) as e:
    sys.stderr.write(f"ERROR: {e}\n")
    sys.exit(1)
