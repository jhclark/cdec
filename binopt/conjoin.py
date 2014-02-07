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
    result.append('='.join(feat1))
    for j in range(i):
      if i != j:
        feat2 = featList[j]
        result.append(feat1[0] + "__" + feat2[0] + "=1")

  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])

