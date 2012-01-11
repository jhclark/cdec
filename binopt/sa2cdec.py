#!/usr/bin/env python
import sys

featNames = [ line.strip() for line in open(sys.argv[1]) if not line.startswith('#') ]

for line in sys.stdin:
  (lhs, src, tgt, feats, align) = line.strip().split(' ||| ')
  featValues = feats.split(' ')
  namedFeats = ' '.join( name+"="+value for (name, value) in zip(featNames, featValues) )
  print " ||| ".join( (lhs, src, tgt, namedFeats, align) )
