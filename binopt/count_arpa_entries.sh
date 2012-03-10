#!/usr/bin/env bash
set -u
set +eo pipefail # we'll get a SIGPIPE from zcat

file=$1

if [ ! -e $file ]; then
    echo >&2 "ERROR: File not found: $file"
    exit 1
fi

count=$(zcat $file | awk -F' |=' '/^ngram/{n+=$3} /\\1-/{print n; exit 0}')
if [[ $# == 2 && "$2" == "--add-unk" ]]; then
    count=$(($count + 1))
fi
echo $count
