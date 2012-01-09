#!/usr/bin/env python
import sys

# TODO: This handles uniform precision variation
#       Now we'd like to base it on previous binning (read in a bin file to determine boundaries)
# TODO: Then we need to update the input file to refer to the new sentence-level grammars

(mode, arg) = sys.argv[1:]
if mode == '--prec':
    prec = arg
    format = "%s@%."+prec+"f"
    def formatFeat(f, val):
        return format%(f,val)
    
elif mode == '--microbinFile':
    binFile = arg
    f = open(binFile)
    f.next() # skip header
    bins = [float(line.strip().split(',')[0]) for line in f]
    f.close()

    print >>sys.stderr, "Read bins:", bins

    from bisect import bisect
    def binary_search(a, x, lo=0, hi=-1):
        i = bisect(a, x, lo, hi)
        closestFloor = bins[max(0,i-1)]
        #print "%f ==> %f"%(x, closestFloor)
        return closestFloor

    format = "%s@%.6e"
    def formatFeat(f, val):
        closestBinFloor = binary_search(bins, val)
        return format%(f,closestBinFloor)
    
else:
    print >>sys.stderr, 'ERROR: Unrecognized mode:', mode
    sys.exit(1)

for line in sys.stdin:
    (lhs, src, tgt, feats, align) = line.strip().split(' ||| ')
    featList = [x.split('=') for x in feats.split()]
    feats = []
    for (f, val) in featList:
        featName = formatFeat(f, float(val))
        feats.append(featName+"=1")
    print ' ||| '.join([lhs, src, tgt, ' '.join(feats), align])
