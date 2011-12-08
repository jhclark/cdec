#!/usr/bin/env python
import sys
import math

for line in sys.stdin:
    try:
        weight = line.strip().split(',')[1]
        weight = abs(float(weight))
        if weight >= 0.01:
            print line,
    except Exception as e:
        print line,
    
