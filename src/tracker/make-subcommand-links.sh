#!/bin/sh

set -e

clicommand=$1
subcommanddir=$2
shift
shift

if [ -d $DESTDIR$subcommanddir ]
then
    for l in `find $DESTDIR$subcommanddir -type l`
    do
	# Delete all previous links to our own binary
	if [ `readlink $l` = "$clicommand" ]
	then
	    rm $l
	fi
    done
fi

mkdir -p $DESTDIR$subcommanddir

for subcommand in $@
do
    ln -s $clicommand $DESTDIR$subcommanddir/$subcommand
done
