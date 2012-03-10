#!/usr/bin/env bash
set -ueo pipefail

count=$(awk -F' |=' '/^ngram/{n+=$3} /\\1-/{print n; exit 0}')
if [[ $# == 1 && "$1" == "--add-unk" ]]; then
    count=$(($count + 1))
fi
echo $count
