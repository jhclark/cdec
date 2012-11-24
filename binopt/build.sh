#!/usr/bin/env bash
set -ueo pipefail
set -x
flags="$@" # e.g. JLM_REAL_VALUES

g++ -g -I$HOME/prefix/include/ -I/home/jhclark/software/cdec/klm -I/home/jhclark/software/cdec/utils -I/home/jhclark/software/cdec/decoder -c build_binary_jlm.cc $flags
g++ -g -lz $HOME/software/cdec/klm/util/file.o $HOME/software/cdec/klm/util/mmap.o $HOME/software/cdec/klm/util/exception.o build_binary_jlm.o -o build_binary_jlm $flags
