#!/usr/bin/env python
import sys
import os.path
import gzip

def zopen(filename, mode='r'):
  if filename.endswith('.gz'):
    return gzip.open(filename, mode)
  else:
    return open(filename, mode)

(genreFile, graDirOut, conjunctionManifest) = sys.argv[1:]
genres = [ genre.strip() for genre in zopen(genreFile) ]
graFiles = [ file.strip() for file in sys.stdin ]

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

conjunctions = set()
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
          conjoinedName = "%s_%s"%(name, genre)
          allFeats.append("%s=%s"%(conjoinedName, value))
          conjunctions.add(conjoinedName)
        feats = ' '.join(allFeats)
        print >>graOut, ' ||| '.join([lhs, src, tgt, feats, align])

f = open(conjunctionManifest, 'w')
f.write('\n'.join(conjunctions))
f.write('\n')
f.close()
