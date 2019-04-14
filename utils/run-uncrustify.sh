#!/bin/bash

# Adapted from Nautilus:
# https://gitlab.gnome.org/GNOME/nautilus/blob/master/data/run-uncrustify.sh

DATA=$(dirname "$BASH_SOURCE")
UNCRUSTIFY=$(command -v uncrustify)

if [ -z "$UNCRUSTIFY" ];
then
    echo "Uncrustify is not installed on your system."
    exit 1
fi

for DIR in {src/libtracker-*,src/tracker,src/tracker-store}
do
    for FILE in $(find "$DIR" -name "*.c")
    do
       if [ "$FILE" != "src/libtracker-fts/fts5.c" ]; then
            "$UNCRUSTIFY" -c "./utils/uncrustify.cfg" --no-backup "$FILE"
       fi
   done
done

