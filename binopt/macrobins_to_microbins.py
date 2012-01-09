#!/home/jhclark/prefix/bin/python
##!/usr/bin/env python
import sys

(binFilename,) = sys.argv[1:]

binFile = open(binFilename)
print binFile.next(),
feats = []
weights = []
for line in binFile:
    if not line.strip():
        continue
    (feat, weight) = line.strip().split(',')
    feats.append(float(feat))
    weights.append(float(weight))
binFile.close()

for line in sys.stdin:
    graFeat = float(line.strip())
    for (i, f) in enumerate(feats):
        if f >= graFeat:
            break
    weight = weights[i]
    print "{},{}".format(line.strip(),weight)
