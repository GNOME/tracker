#!/bin/sh

set -e

bindir=$MESON_INSTALL_PREFIX/bin
libexecdir=$MESON_INSTALL_PREFIX/libexec

if [ -d $libexecdir/tracker ]
then
    for l in `find $libexecdir/tracker -type l`
    do
	# Delete all previous links to our own binary
	if [[ `readlink $l` = "$bindir/tracker" ]]
	then
	    rm $l
	fi
    done
fi

mkdir -p $libexecdir/tracker

for subcommand in $@
do
    ln -s $bindir/tracker $libexecdir/tracker/$subcommand
done
