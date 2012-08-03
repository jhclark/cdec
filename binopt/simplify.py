#!/usr/bin/env python
import sys
import math

modes = sys.argv[1:]

def die(msg):
  print >>sys.stderr, msg
  sys.exit(1)

for mode in modes:
  majorModes = [m for m in modes if m != 'unlog']
  if mode == 'unlog':
    print >>sys.stderr, "Reversing log transforms..."
  elif mode == 'no_phrasal':
    print >>sys.stderr, "Removing phrasal translation feature..."
  elif mode == 'phrasal_only':
    print >>sys.stderr, "Keeping only the phrasal translation feature..."
    if len(majorModes) > 1: die("phrasal_only is not compatible with other major modes")
  elif mode == 'counts_only':
    print >>sys.stderr, "Keeping only the count features..."
    if len(majorModes) > 1: die("phrasal_only is not compatible with other major modes")
  else:
    print >>sys.stderr, "ERROR: Unrecognized mode:", mode
    sys.exit(1)

# sa-extract uses -math.log10(prob) and floors at 99
# note: reads and writes strings
def unlog(value):
  if 'unlog' in modes:
    value = float(value)
    if value >= 99:
      return 0
    else:
      # We're only getting a couple of digits for lexical probabilities even with a precision of 20
      # but that appears to be enough even when creating 1000 bins
      return "%.30f"%math.pow(10,-value)
  else:
    return value

def unlogCount(value):
  if 'unlog' in modes:
    value = float(value)
    return "%.0f"%(math.pow(10,value)-1)
  else:
    return value

for line in sys.stdin:
  (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
  featList = [x.split('=') for x in feats.split()]

  # Remove log transform from all sa-align features:
  # EGivenFCoherent: -log10(PairCount / SampleCount) (sa-extract/context_model.py:100)
  # SampleCountF = math.log10(1.0 + float(fsample_count))
  # MaxLexFGivenE
  # MaxLexEGivenF
  result = []
  for (name, value) in featList:
    if 'phrasal_only' in modes and name != "EGivenFCoherent":
        continue
    if 'counts_only' in modes and name != 'SampleCountF' and name != 'CountEF':
        continue
    if 'no_phrasal' in modes and name == 'EGivenFCoherent':
      continue

    if name == 'MaxLexEGivenF' or name == 'MaxLexFGivenE' or name == 'EGivenFCoherent':
      result.append(name + "=" + unlog(value))
    elif name == 'SampleCountF' or name == 'CountEF':
      result.append(name + "=" + unlogCount(value))
    else:
      result.append(name + "=" + value)
  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])
