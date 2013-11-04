#!/usr/bin/env python
import sys
import gzip
import itertools

# f = source
# e = target
# g = genres
(fSeen, fFileIn, eFileIn, gFileIn, fFileOut, eFileOut, gFileOut) = sys.argv[1:]

# Record which source sentences we've seen so far
# so that we can eliminate exact duplicates
with gzip.open(fSeen) as fIn:
     seen = set([line for line in fIn])

# Finally, iterate over the training data
numDups = 0
nLines = 0
print "Reading file", fFileIn
print "Reading file", eFileIn
print "Reading file", gFileIn
with gzip.open(fFileIn, 'r') as fIn, \
   gzip.open(fFileOut, 'w') as fOut, \
   gzip.open(eFileIn, 'r') as eIn, \
   gzip.open(eFileOut, 'w') as eOut, \
   gzip.open(gFileIn, 'r') as gIn, \
   gzip.open(gFileOut, 'w') as gOut:
     for (fLine, eLine, gLine) in itertools.izip_longest(fIn, eIn, gIn):
          if not fLine: raise Exception("Not enough lines in file: " + fFileIn)
          if not eLine: raise Exception("Not enough lines in file: " + eFileIn)
          if not gLine: raise Exception("Not enough lines in file: " + gFileIn)

          nLines += 1
          if fLine in seen:
               numDups += 1
          else:
               fOut.write(fLine)
               eOut.write(eLine)
               gOut.write(gLine)
               # We don't record additional seen sentences in the training set because:
               # 1) We don't want to internally dedupe in the training set
               # 2) That would be a very large set to hold in memory :)

dupsPercent = float(numDups) / float(nLines) * 100
print "Removed " + str(numDups) + " training set duplicates seen in tune or test set: " + str(dupsPercent) + "%"
