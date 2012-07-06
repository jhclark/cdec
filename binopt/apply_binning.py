#!/usr/bin/env python
import sys
import math
from collections import defaultdict

(bin_info_file, mode) = sys.argv[1:]

def die(msg):
  print >>sys.stderr, msg
  sys.exit(1)

if mode == 'indicator':
  print >>sys.stderr, "Using indicator bin features"
  useIndicators = True
elif mode == 'real':
  print >>sys.stderr, "Using real-valued bin features"
  useIndicators = False
else:
  die("Unrecognized mode: " + mode)

# featName -> [ (destFeatName, lowVal, highVal) ]
# destFeatName fires iff lowVal < featVal <= highVal
# -inf and inf are legal bounds
# multiple destFeats may fire for a single original feature
# original features are removed after binning
all_bin_info = defaultdict(list)

# Incoming lines look like this:
# bin MaxLexEGivenF MaxLexEGivenF_-0.000000_0.916822_Overlap0 -inf <= x < 0.916942 [ -0.000000 - 0.916822 count=48950 adjCount=48950 L=35 R=70 ]
f = open(bin_info_file)
for line in f:
  cols = line.strip().split(' ')
  featName = cols[1]
  destFeatName = cols[2]
  lowValue = float(cols[3])
  highValue = float(cols[7])
  all_bin_info[featName].append( (destFeatName, lowValue, highValue) )
f.close()

for line in sys.stdin:
  (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
  featList = [x.split('=') for x in feats.split()]

  # Remove log transform from all sa-align features:
  # MaxLexEGivenF
  # EGivenFCoherent: -log10(PairCount / SampleCount) (sa-extract/context_model.py:100)
  # MaxLexFGivenE
  result = []
  for (name, strValue) in featList:
    value = float(strValue)
    try:
      my_bin_info = all_bin_info[name]
      for (destFeatName, lowValue, highValue) in my_bin_info:
        if lowValue < value and value <= highValue:
          if useIndicators:
            destValue = "1"
          else:
            destValue = strValue
          result.append(destFeatName + "=" + destValue)
    except KeyError:
      die("Unrecognized feature: " + name)
  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])
