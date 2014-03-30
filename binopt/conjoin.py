#!/usr/bin/env python
import sys

def die(msg):
  print >>sys.stderr, msg
  sys.exit(1)

for line in sys.stdin:
  (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
  featList = [x.split('=') for x in feats.split()]

  result = []
  # Conjoin
  for i in range(len(featList)):
    feat1 = featList[i]
    
    # Don't append source-only "FullSrc*" features (see add_hfw_feats.py)
    # unless it's already been conjoined (i.e. there's a __ in the feature name)
    isUnconjoinedFullSrc = feat1[0].startswith("FullSrc") and not '__' in feat1[0]
    isAlignedFeat = 'Aligned' in feat1[0]
    if not isUnconjoinedFullSrc and not isAlignedFeat:
      result.append('='.join(feat1))
    
    for j in range(i):
      if i != j:
        feat2 = featList[j]
        result.append(feat1[0] + "__" + feat2[0] + "=1")

  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])

