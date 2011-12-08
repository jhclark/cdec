#!/usr/bin/env python
import sys

weights = [0.1, 0.2, 0.1, 0.1, 0.16, 0.2, 0.25, 0.25, 0.3, 0.25]

print "Orig Weights: {0}".format(weights)

# Bounded depth-first search
dp = []
for i in range(len(weights)):
    states = [] # has (binCount, cost, prevWeight, backpointer)
    w = weights[i]
    if i == 0:
        states.append( (1, 0, w, -1) )
    else:
        for (j, (binCount, prevCost, prevWeight, dummyBP)) in enumerate(dp[i-1]):
            if w == prevWeight:
                # Don't need a new bin
                states.append( (binCount, prevCost, w, j) )
            else:
                # First, try making a new bin
                states.append( (binCount+1, prevCost, w, j) )

                # Now try expanding the current bin
                cost = abs(prevWeight - w)
                states.append( (binCount, prevCost+cost, prevWeight, j) )
    # Beam number of previous states?
    dp.append(states)

# TODO: Try an averaging strategy over spans?

def backtrace(i, backpointer):
    (binCount, cost, weight, nextBP) = dp[i][backpointer]
#    print 'backtrace', i, backpointer, binCount, cost, weight, nextBP
    if i == 0:
        return [weight] # We can never change this first weight?!
    else:
        return backtrace(i-1, nextBP) + [weight]

print "Final states: {0}".format(len(dp[-1]))
#for i in range(len(dp)):
#    for j in range(min(5,len(dp[i]))):
#        print i, j, dp[i][j]       

curCount = 1
for ((binCount, cost, weight, backpointer), selfpointer) in sorted(zip(dp[-1], range(len(dp[-1])))):
    if curCount == binCount:
        print "Final weight: {0}; Bin Count: {1};  Final cost: {2}".format(weight, binCount, cost)
        print "Weights: {0}".format(backtrace(len(dp)-1, selfpointer))
        print
        curCount += 1
