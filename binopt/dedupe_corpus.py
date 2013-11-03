#!/usr/bin/env python
import sys
import gzip
import itertools

# f = source
# e = target
# g = genres
(fTrainIn, eTrainIn, gTrainIn, fTuneIn, eTuneIn, gTuneIn, fTestIn, eTestIn, gTestIn,
 fTrainOut, eTrainOut, gTrainOut, fTuneOut, eTuneOut, gTuneOut, fTestOut, eTestOut, gTestOut) = sys.argv[1:]

# Record which source sentences we've seen so far
# so that we can eliminate exact duplicates
seen = set()

# First, load test set
# There's actually no reason to write out a new test set at the moment
# This placeholder is here in case we ever decide to do something fancier in the future
# while not breaking the interface
print "Reading file", fTestIn
print "Reading file", eTestIn
print "Reading file", gTestIn
with gzip.open(fTestIn, 'r') as fIn, \
   gzip.open(fTestOut, 'w') as fOut, \
   gzip.open(eTestIn, 'r') as eIn, \
   gzip.open(eTestOut, 'w') as eOut, \
   gzip.open(gTestIn, 'r') as gIn, \
   gzip.open(gTestOut, 'w') as gOut:
     for (fLine, eLine, gLine) in itertools.izip_longest(fIn, eIn, gIn):
          if not fLine: raise Exception("Not enough lines in file:" + fTestIn)
          if not eLine: raise Exception("Not enough lines in file:" + eTestIn)
          if not gLine: raise Exception("Not enough lines in file:" + gTestIn)

          fOut.write(fLine)
          eOut.write(eLine)
          gOut.write(gLine)
          seen.add(fLine)

# Next, iterate over tune set, deduping
tuneDups = 0
nTune = 0
with gzip.open(fTuneIn, 'r') as fIn, \
   gzip.open(fTuneOut, 'w') as fOut, \
   gzip.open(eTuneIn, 'r') as eIn, \
   gzip.open(eTuneOut, 'w') as eOut, \
   gzip.open(gTuneIn, 'r') as gIn, \
   gzip.open(gTuneOut, 'w') as gOut:
     for (fLine, eLine, gLine) in itertools.izip_longest(fIn, eIn, gIn):
          if not fLine: raise Exception("Not enough lines in file: " + fTuneIn)
          if not eLine: raise Exception("Not enough lines in file:" + eTuneIn)
          if not gLine: raise Exception("Not enough lines in file:" + gTuneIn)

          nTune += 1
          if fLine in seen:
               tuneDups += 1
          else:
               fOut.write(fLine)
               eOut.write(eLine)
               gOut.write(gLine)
               seen.add(fLine)

tunePercent = float(tuneDups) / float(nTune) * 100
print "Removed " + str(tuneDups) + " tune set duplicates seen in test set: " + str(tunePercent) + "%"

# Finally, iterate over the training data
trainDups = 0
nTrain = 0
print "Reading file", fTrainIn
print "Reading file", eTrainIn
print "Reading file", gTrainIn
with gzip.open(fTrainIn, 'r') as fIn, \
   gzip.open(fTrainOut, 'w') as fOut, \
   gzip.open(eTrainIn, 'r') as eIn, \
   gzip.open(eTrainOut, 'w') as eOut, \
   gzip.open(gTrainIn, 'r') as gIn, \
   gzip.open(gTrainOut, 'w') as gOut:
     for (fLine, eLine, gLine) in itertools.izip_longest(fIn, eIn, gIn):
          if not fLine: raise Exception("Not enough lines in file:" + fTrainIn)
          if not eLine: raise Exception("Not enough lines in file:" + eTrainIn)
          if not gLine: raise Exception("Not enough lines in file:" + gTrainIn)

          nTrain += 1
          if fLine in seen:
               trainDups += 1
          else:
               fOut.write(fLine)
               eOut.write(eLine)
               gOut.write(gLine)
               # We don't record additional seen sentences in the training set because:
               # 1) We don't want to internally dedupe in the training set
               # 2) That would be a very large set to hold in memory :)

trainPercent = float(trainDups) / float(nTrain) * 100
print "Removed " + str(trainDups) + " training set duplicates seen in tune or test set: " + str(trainPercent) + "%"
