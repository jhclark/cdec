#!/usr/bin/env python
import sys

for line in sys.stdin:
    toks = ['<s>'] + line.strip().split() + ['</s>']
    for i in range(len(toks)-1):
        print ' '.join(toks[i:i+2])
