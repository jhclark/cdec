#!/usr/bin/env python
import sys
import gzip
import itertools

(srcFileIn, refFileIn, genreFileIn, srcFileOut, refFileOut, genreFileOut, numRefs, minWords) = sys.argv[1:]

if int(numRefs) != 1:
    raise "Currently only supports one reference"

srcLines = [line for line in gzip.open(srcFileIn, 'r')]
refLines = [line for line in gzip.open(refFileIn, 'r')]
genreLines = [line for line in gzip.open(genreFileIn, 'r')]

if len(srcLines) != len(refLines): raise "Src and refs not parallel"
if len(srcLines) != len(genreLines): raise "Src and genres not parallel"

with gzip.open(srcFileOut, 'w') as srcOut:
    with gzip.open(refFileOut, 'w') as refOut:
        with gzip.open(genreFileOut, 'w') as genreOut:
            for (src, ref, genre) in itertools.izip_longest(srcLines, refLines, genreLines):
                if len(src.split()) >= int(minWords):
                    srcOut.write(src)
                    refOut.write(ref)
                    genreOut.write(genre)
