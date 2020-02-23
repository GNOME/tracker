#!/bin/sh

set -e

clicommand=$1
subcommanddir=$2
shift
shift

if [ -d $subcommanddir ]
then
    for l in `find $subcommanddir -type l`
    do
	# Delete all previous links to our own binary
	if [ `readlink $l` = "$clicommand" ]
	then
	    rm $l
	fi
    done
fi

mkdir -p $subcommanddir

for subcommand in $@
do
    ln -s $clicommand $subcommanddir/$subcommand
done
