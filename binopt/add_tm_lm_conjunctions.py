#!/usr/bin/env python
import sys
import os.path
import gzip

def zopen(filename, mode='r'):
  if filename.endswith('.gz'):
    return gzip.open(filename, mode)
  else:
    return open(filename, mode)

(graDirOut, conjunctionManifest) = sys.argv[1:]
graFiles = [ file.strip() for file in sys.stdin ]

def sentIdFromGra(filename):
  if filename.endswith('.gz'):
    return int(filename.split('.')[-2])
  else:
    return int(filename.split('.')[-1])
  
# Now sort gra files by their numeric extension
graFiles = sorted(graFiles, key=sentIdFromGra)

conjunctions = set()
for graFileIn in graFiles:
  graFileOut = "{}/{}".format(graDirOut, os.path.basename(graFileIn))
  print >>sys.stderr, "Writing {}".format(graFileOut)
  with zopen(graFileIn) as graIn:
    with zopen(graFileOut, 'w') as graOut:
      for line in graIn:
        (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')

        # In our case, the discretization "destination feature names" should all start with a double underscore
        # This allows us to use only those discretized features by:
        # 1) Searching for startswith("__FEATNAME")
        # 2) Creating a single LM conjunction ("LM_Trigger__FEATNAME") -- remember to write this to the manifest
        # 3) Removing all of the discretized features that started with __

        components = [] # Components for conjoining into the LM trigger feat
        otherFeats = []
        for featPair in feats.split():
          (name, value) = featPair.split('=')
          if name.startswith('__'):
            # This is a component of the LM trigger feature
            assert(float(value) == 1.0)
            components.append(name)
          else:
            otherFeats.append(featPair)
        lmTriggerFeat = 'LM_Trigger__{}=1'.format('_'.join(components))
            
        conjunctions.add(lmTriggerFeat) # Save LM trigger feature to conjunction manifest

        allFeats = ' '.join( otherFeats + [lmTriggerFeat] )
        print >>graOut, ' ||| '.join([lhs, src, tgt, allFeats, align])

f = open(conjunctionManifest, 'w')
f.write('\n'.join(conjunctions))
f.write('\n')
f.close()
