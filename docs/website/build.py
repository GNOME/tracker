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


import argparse
import logging
import pathlib
import shutil
import subprocess
import sys
import tempfile

log = logging.getLogger('build.py')

output_path = pathlib.Path('public')
mkdocs_root = pathlib.Path(__file__).parent.parent


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


def main():
    args = argument_parser().parse_args()

    if args.debug:
        logging.basicConfig(stream=sys.stderr, level=logging.DEBUG)
    else:
        logging.basicConfig(stream=sys.stderr, level=logging.INFO)

    if output_path.exists():
        raise RuntimeError(f"Output path '{output_path}' already exists.")

    log.info("Building website")
    mkdocs_config = mkdocs_root.joinpath('mkdocs.yml')
    subprocess.run(['mkdocs', 'build', '--config-file', mkdocs_config,
                    '--site-dir', output_path.absolute()])

    apidocs_src = pathlib.Path(args.api_docs)

    log.info("Copying API reference documentation from %s", apidocs_src)
    apidocs_dest = output_path.joinpath('docs/api-preview')
    apidocs_dest.mkdir(parents=True)
    for name in ['libtracker-sparql', 'ontology']:
        shutil.copytree(apidocs_src.joinpath(name), apidocs_dest.joinpath(name))

    log.info("Adding preview header to API reference documentation")
    text = apidocs_header(args.tracker_commit)
    for filename in apidocs_dest.rglob('*.html'):
        add_apidocs_header(text, filename)

    log.info("Documentation available in public/ directory.")


try:
    main()
except RuntimeError as e:
    sys.stderr.write(f"ERROR: {e}\n")
    sys.exit(1)
