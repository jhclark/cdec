#!/usr/bin/env python
import sys
import math

modes = sys.argv[1:]

for line in sys.stdin:
  (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
  featList = [x.split('=') for x in feats.split()]
  f = 0
  ef = 0
  for name, val in featList:
      if name == 'SampleCountF':
          f = float(val)
      elif name == 'CountEF':
          ef = float(val)    
  tgs = ef / f
  
  featList = []
  for mode in modes:
    if mode == 'prob':
      featList.append('PTGS=%.6f'%tgs)
    elif mode == 'log':
      lp = -math.log10(tgs)
      featList.append('LogPTGS=%.6f'%lp)
  feats = ' '.join(featList)
  print ' ||| '.join([lhs, src, tgt, feats, align])
