#!/usr/bin/env python
import sys
import math

modes = sys.argv[1:]

def die(msg):
  print >>sys.stderr, msg
  sys.exit(1)

for mode in modes:
  if mode == 'unlog':
    print >>sys.stderr, "Reversing log transforms..."
  elif mode == 'no_phrasal':
    print >>sys.stderr, "Removing phrasal translation feature..."
  elif mode == 'phrasal_only':
    print >>sys.stderr, "Keeping only the phrasal translation feature..."
    if len(modes) > 1: die("phrasal_only is not compatible with other options")
  elif mode == 'counts_only':
    print >>sys.stderr, "Keeping only the count features..."
    if len(modes) > 1: die("phrasal_only is not compatible with other options")
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
    if 'phrasal_only' in modes and name != "EGivenFCoherent":
        continue
    if 'counts_only' in modes and name != 'SampleCountF' and name != 'CountEF':
        continue
    if 'no_phrasal' in modes and name == 'EGivenFCoherent':
      continue

    if name == 'MaxLexEGivenF' or name == 'MaxLexFGivenE' or name == 'EGivenFCoherent':
      result.append( (name, unlog(value) ) )
    else:
      result.append( (name, value) )
  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])
