#!/bin/bash
#
# Test runner script for Tracker's functional tests

set -e

SCRIPT=$1

export TEMP_DIR=`mktemp --tmpdir -d tracker-test-XXXX`

# We need to use the actual home directory for some tests because
# Tracker will explicitly ignore files in /tmp ...
export REAL_HOME=`echo ~`

# ... but /tmp is preferred for test data, to avoid leaving debris
# in the filesystem
HOME=$TEMP_DIR

echo "Running $@"
dbus-run-session -- "$@"

rm -R $TEMP_DIR
