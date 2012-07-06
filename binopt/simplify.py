#!/usr/bin/env python
import sys
import math

modes = sys.argv[1:]

for mode in modes:
  if mode == 'unlog':
    print >>sys.stderr, "Reversing log transforms..."
  elif mode == 'simple':
    print >>sys.stderr, "Keeping only the phrasal translation feature..."
  else:
    print >>sys.stderr, "ERROR: Unrecognized mode:", mode
    sys.exit(1)

# sa-extract uses -math.log10(prob) and floors at 99
# note: reads and returns strings
def unlog(value):
  if 'unlog' in modes:
    value = float(value)
    if value >= 99:
      return '0.0'
    else:
      return str(math.pow(10,-value))

for line in sys.stdin:
  (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
  featList = [x.split('=') for x in feats.split()]

  # Remove log transform from all sa-align features:
  # MaxLexEGivenF
  # EGivenFCoherent: -log10(PairCount / SampleCount) (sa-extract/context_model.py:100)
  # MaxLexFGivenE
  result = []
  for (name, value) in featList:
    if 'simple' in modes:
      if name != "EGivenFCoherent":
        continue

    if name == 'MaxLexEGivenF' or name == 'MaxLexFGivenE' or name == 'EGivenFCoherent':
      result.append( (name, unlog(value) ) )
    else:
      result.append( (name, value) )
  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])
