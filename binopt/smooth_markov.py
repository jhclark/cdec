#!/usr/bin/env python
import sys

print sys.stdin.next(),

weights = []
for line in sys.stdin:
    (feat, weight) = line.strip().split(',')
    weight = float(weight)
    weights.append( (weight, line) )

def avg(vec):
    return sum(vec) / len(vec)

order = 5

lines = []
lines.append(weights[0][1])
for i in range(1,len(weights)-1):
    leftNeighbors = [ w for (w,_) in weights[max(0,i-order):i] ]
    rightNeighbors = [ w for (w,_) in weights[i+1:min(len(weights),i+order)] ]
    neighbors = leftNeighbors + rightNeighbors

    least = avg(leftNeighbors)
    most = avg(rightNeighbors)

    #least = min(neighbors)
    #most = max(neighbors)

    actual = weights[i][0]
    #print >>sys.stderr, 'got', actual, least, most, 'AT', weights[i][1]
    #print >>sys.stderr, leftNeighbors
    #print >>sys.stderr, rightNeighbors
    if actual < least or actual > most:
         print >>sys.stderr, 'removed', actual, least, most
    else:
        lines.append(weights[i][1])

for line in lines:
    print line,
    
    
    
