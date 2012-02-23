#!/usr/bin/env python
import sys
import os.path

(genreFile, graDirOut) = sys.argv[1:]
genres = [genre.strip() for genre in open(genreFile)]
graFiles = [file.strip() for file in sys.stdin]

# Now sort gra files by their numeric extension
graFiles = sorted(graFiles, key=lambda x: int(x.split('.')[-1]))

if len(graFiles) != len(genres):
  print >>sys.stderr, 'ERROR: Mismatch: Got {} grammars and {} genres'.format(len(graFiles), len(genres))
  sys.exit(1)

for (graFileIn, genre) in zip(graFiles, genres):
  graFileOut = "{}/{}".format(graDirOut, os.path.basename(graFileIn))
  print >>sys.stderr, "Writing {}".format(graFileOut)
  with open(graFileIn) as graIn:
    with open(graFileOut, 'w') as graOut:
      for line in graIn:
        (lhs, src, tgt, feats, align) = line.strip().split(' ||| ')
        featList = feats.split()
        allFeats = list(featList)
        for featPair in featList:
          (name, value) = featPair.split('=')
          allFeats.append("%s_%s=%s"%(name, genre, value))
        feats = ' '.join(allFeats)
        print >>graOut, ' ||| '.join([lhs, src, tgt, feats, align])
