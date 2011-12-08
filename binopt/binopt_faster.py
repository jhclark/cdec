#!/usr/bin/env python
import sys
import random

weights = [random.random() for i in xrange(500)]
weights = [i/N for i in xrange(500)]
#weights = [0.1, 0.2, 0.1, 0.1, 0.16, 0.2, 0.25, 0.25, 0.3, 0.25]
K = 2
MAX_BINS = 10

print "Orig Weights: {0}".format(weights)

def avg(L): return sum(L) / len(L)

def cost(a,b):
    if a == b:
        return 0
    else:
        bin = weights[a:b+1]
        av = sum(bin) / (b-a)
        return sum( pow(av-w,2) for w in bin)

def search(start, end):
    # has [I][binCount]->(pathCost, costEst, curBinStart, backpointer)
    dp = []
    for i in range(len(weights)):
        states = [list() for x in range(MAX_BINS)]
        w = weights[i]
        if i == 0:
            states[0].append( (0, 0, 0, None) )
        else:
            for binCount in xrange(MAX_BINS):
                for (j, (pathCost, costEst, curBinStart, prevBP)) in enumerate(dp[i-1][binCount]):
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
        print i
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
    print 'Bin:',curBinStart, i, '--', origBin, '->', binWeight
    if curBinStart == 0:
        return [binWeight]*binSpan
    else:
        (nextBinCount, nextIdx) = nextBP
        return backtrace(curBinStart-1, nextBinCount, nextIdx) + [binWeight]*binSpan

dp = search(0, len(weights))

print "Final states: {0}".format(len(dp[-1]))
#for i in range(len(dp)):
#    for j in range(min(5,len(dp[i]))):
#        print i, j, dp[i][j]       

for binCount in xrange(MAX_BINS):
    for ((pathCost, costEst, curBinStart, backpointer), selfpointer) in sorted(zip(dp[-1][binCount], range(len(dp[-1]))))[:1]:
        print "Bin Count: {0};  Final cost: {1}".format(binCount, pathCost)
        bt = backtrace(len(dp)-1, binCount, selfpointer)
        #print "Weights: {0}".format(bt)
        print
