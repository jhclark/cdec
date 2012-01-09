#!/home/jhclark/prefix/bin/python
##!/usr/bin/env python
import sys

(macrobinFilename, graValuesFilename, valuesPerBin) = sys.argv[1:]
valuesPerBin = int(valuesPerBin)

f = open(macrobinFilename)
f.next()
macrobins = [map(float, line.strip().split(',')) for line in f]
f.close()

f = open(graValuesFilename)
f.next()
graValues = [float(line.strip()) for line in f]
f.close()

inf = float("inf")

for i in range(len(macrobins)):
    weight = macrobins[i][1]
    low = macrobins[i][0]
    high = macrobins[i+1][0] if i < len(macrobins)-1 else inf
    # TODO: This could be made more efficient
    candidates = filter(lambda x: x >= low and x <= high, graValues)
    step = float(valuesPerBin) / float(len(candidates))
    accum = 0.0
    prevIdx = -1
    selected = []
    for x in candidates:
        if int(accum) != prevIdx:
            prevIdx = int(accum)
            selected.append(x)
        accum += step
    print >>sys.stderr, low, high, len(candidates), "candidates", len(selected), "selected"
    for x in selected:
        print "%f,%f"%(x,weight)
