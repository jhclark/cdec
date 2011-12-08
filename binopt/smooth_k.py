#!/usr/bin/env python
import sys

# 1) Rank by weight
# 2) Select k most extreme points (possibly with some bias to make sure we cover at least the endpoints)

(N,) = sys.argv[1:]
N = int(N)

print sys.stdin.next(),

weights = []
for line in sys.stdin:
    (feat, weight) = line.strip().split(',')
    weight = abs(float(weight))
    weights.append( (weight, line) )

weights.sort(reverse=True)
lines = [ line for (_, line) in weights[:N] ]
for line in sorted(lines):
    print line,
    
    
    
