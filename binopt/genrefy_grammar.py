#!/usr/bin/env python
import sys
import os.path
import gzip

(genreFile, graDirOut) = sys.argv[1:]
genres = [genre.strip() for genre in open(genreFile)]
graFiles = [file.strip() for file in sys.stdin]

def sentIdFromGra(filename):
  if filename.endswith('.gz'):
    return int(filename.split('.')[-2])
  else:
    return int(filename.split('.')[-1])
  
# Now sort gra files by their numeric extension
graFiles = sorted(graFiles, key=sentIdFromGra)

if len(graFiles) != len(genres):
  print >>sys.stderr, 'ERROR: Mismatch: Got {} grammars and {} genres'.format(len(graFiles), len(genres))
  sys.exit(1)

def zopen(filename, mode='r'):
  if filename.endswith('.gz'):
    return gzip.open(filename, mode)
  else:
    return open(filename, mode)

for (graFileIn, genre) in zip(graFiles, genres):
  graFileOut = "{}/{}".format(graDirOut, os.path.basename(graFileIn))
  print >>sys.stderr, "Writing {}".format(graFileOut)
  with zopen(graFileIn) as graIn:
    with zopen(graFileOut, 'w') as graOut:
      for line in graIn:
        (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
        featList = feats.split()
        allFeats = list(featList)
        for featPair in featList:
          (name, value) = featPair.split('=')
          allFeats.append("%s_%s=%s"%(name, genre, value))
        feats = ' '.join(allFeats)
        print >>graOut, ' ||| '.join([lhs, src, tgt, feats, align])
