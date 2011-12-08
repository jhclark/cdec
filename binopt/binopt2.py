#!/usr/bin/env python
import sys
import random

(K, MAX_BINS) = sys.argv[1:]

print sys.stdin.next(),
feats = []
weights = []
for line in sys.stdin:
    if not line.strip():
        continue
    (feat, weight) = line.strip().split(',')
    feats.append(float(feat))
    weights.append(float(weight))

#weights = [random.random() for i in xrange(1000)]
#weights = [0.1, 0.2, 0.1, 0.1, 0.16, 0.2, 0.25, 0.25, 0.3, 0.25]
K = int(K) # 10
MAX_BINS = int(MAX_BINS) #10

print >>sys.stderr, "Orig Weights: {0}".format(weights)

def avg(L): return sum(L) / len(L)

def cost(a,b):
    if a == b:
        return 0
    else:
        bin = weights[a:b+1]
        av = sum(bin) / (b-a)
        return sum( pow(av-w,2) for w in bin)

def search(start, end):
    # Bounded depth-first search
    dp = []
    for i in range(len(weights)):
        states = [list() for x in range(MAX_BINS)] # has [binCount]->(pathCost, costEst, curBinStart, backpointer)
        w = weights[i]
        if i == 0:
            states[0].append( (0, 0, 0, None) )
        else:
            for binCount in xrange(MAX_BINS):
                for (j, (pathCost, costEst, curBinStart, prevBP)) in enumerate(dp[i-1][binCount]):
                    if i > 0 and w == weights[i-1]:
                        # Don't need a new bin
                        states[binCount].append( (pathCost, costEst, curBinStart, prevBP) )
                    else:
                        # First, try making a new bin
                        if binCount < MAX_BINS-1:
                            if i < len(weights)-1:
                                states[binCount+1].append( (pathCost+costEst, cost(i,i), i, (binCount, j)) )
                            else:
                                states[binCount+1].append( (pathCost+costEst+cost(i,i), 0, i, (binCount, j)) )

                            # Now try expanding the current bin
                            if i < len(weights)-1:
                                states[binCount].append( (pathCost, cost(curBinStart, i), curBinStart, prevBP) )
                            else:
                                states[binCount].append( (pathCost+cost(curBinStart, i), 0, curBinStart, prevBP) )
        if i % 100 == 0: print >>sys.stderr, 'Searching at', i
        for binCount in xrange(MAX_BINS):
            states[binCount] = sorted(states[binCount], key=lambda x: x[0]+x[1])[:K]
            #print 'Hypothesized',len(states[binCount]),'states for bin',binCount,'at',i
        dp.append(states)
    return dp

def backtrace(i, binCount, backpointer):
    (pathCost, costEst, curBinStart, nextBP) = dp[i][binCount][backpointer]
    #print 'backtrace', i, backpointer, '--', binCount, pathCost, costEst, curBinStart, nextBinBP, nextIdxBP
    binSpan = i - curBinStart + 1
    origBin = weights[curBinStart:i+1]
    binWeight = avg(origBin)
    #print 'span/weight', binSpan, binWeight, weights[curBinStart:i+1]
    print >>sys.stderr, 'Bin:',curBinStart, i, '--', origBin[:10], '->', binWeight
    if curBinStart == 0:
        return [binWeight]*binSpan
    else:
        (nextBinCount, nextIdx) = nextBP
        return backtrace(curBinStart-1, nextBinCount, nextIdx) + [binWeight]*binSpan

dp = search(0, len(weights))
print >>sys.stderr, "Final states: {0}".format(len(dp[-1]))
#for i in range(len(dp)):
#    for j in range(min(5,len(dp[i]))):
#        print i, j, dp[i][j]       

for binCount in xrange(MAX_BINS):
    for ((pathCost, costEst, curBinStart, backpointer), selfpointer) in sorted(zip(dp[-1][binCount], range(len(dp[-1]))))[:1]:
        print >>sys.stderr, "Bin Count: {0};  Final cost: {1}".format(binCount, pathCost)
        bt = backtrace(len(dp)-1, binCount, selfpointer)
        #print "Weights: {0}".format(bt)
        print >>sys.stderr
        if binCount == MAX_BINS-1:
            binsOnly = True
            if not binsOnly:
                # Print a line for each original point
                for (feat, weight) in zip(feats, bt):
                    print "%s,%f"%(feat,weight)
            else:
                bins = []
                prevWeight = None
                for (feat, weight) in zip(feats, bt):
                    if weight != prevWeight:
                        bins.append( (feat, weight) )
                        prevWeight = weight
                # Just print the bins
                for (feat, weight) in bins:
                    print "%s,%f"%(feat,weight)

                

