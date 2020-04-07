#!/bin/sh

set -e

cli_dir=$MESON_BUILD_ROOT/$MESON_SUBDIR
subcommands_dir=$MESON_BUILD_ROOT/$MESON_SUBDIR/subcommands

mkdir -p $subcommands_dir

for l in `find $subcommands_dir -type l`
do
# Delete all previous links to our own binary
if [ `readlink $l` = "$cli_dir/tracker3" ] || [ `readlink $l` = "$cli_dir/tracker" ];
then
    rm $l
fi
done

for subcommand in $@
do
    ln -s $cli_dir/tracker3 $subcommands_dir/$subcommand
done
