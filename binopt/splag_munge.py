#!/home/jhclark/prefix/bin/python
##!/usr/bin/env python
import sys
from collections import defaultdict

(binnedFile, featName) = sys.argv[1:]
f = open(binnedFile)
f.next()
bins = []
for line in f:
    (val, weight) = line.strip().split(',')
    bins.append(float(val))
f.close()
print >>sys.stderr, "Have %d bins"%len(bins)
if len(bins) == 0:
    print >>sys.stderr, "Need more than zero bins"
    sys.exit(1)

def munge(featPrefix, val):
    graFeat = float(val)
    prevVal = 0.0
    myVal = 0.0
    for (i, myVal) in enumerate(bins):
        if myVal >= graFeat:
            break
        prevVal = myVal
    mapToHigh = True # Makes conversion munging easier (just use highBA point of range)
    if mapToHigh:
        return "%s@%f"%(featPrefix, myVal)
    else:
        return "%s@%f_%f"%(featPrefix, prevVal, myVal)

for line in sys.stdin:
    (label, feats) = line.strip().split('\t')
    outFeats = defaultdict(lambda: 0.0)
    for feat in feats.split(' '):
        (name, val) = feat.split('=')
        val = float(val)
        if '@' not in feat:
            outFeats[name] = val
        else:
            (f,pos) = name.split('@')
            newName = munge(f, pos)
            outFeats[newName] += val
    print "%s\t%s"%(label, ' '.join(["%s=%f"%(name,val) for (name,val) in outFeats.iteritems()]))
