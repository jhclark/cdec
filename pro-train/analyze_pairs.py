#!/usr/bin/env python
import sys
from collections import defaultdict

weightsFile = sys.argv[1]
weights = defaultdict(lambda: 0.0)
for line in open(weightsFile):
    line = line.strip()
    if not line.startswith('#'):
        (name, val) = line.split()
        weights[name] = float(val)

numCorrect = 0
numIncorrect = 0

numCorrectWithPass = 0

i = 0
for line in sys.stdin:
    i += 1
    if i % 2 == 0:
        (posNeg, feats) = line.strip().split('\t')
        assert posNeg == '1' or posNeg == '0'
        positive = posNeg == '1'
        
        dot = 0.0
        wp = False
        for x in feats.split():
            (name, val) = x.split('=')
            weight = weights[name]
            dot += weight * float(val)
            if name == 'PassThrough':
                wp = True
        if (dot > 0 and positive) or (dot < 0 and not positive):
            numCorrect += 1
            if wp:
                numCorrectWithPass += 1
                print posNeg, feats
        else:
            numIncorrect += 1

total = numCorrect + numIncorrect
print 'Total pairs:', total
print 'Correct:     %d %.1f%%'%(numCorrect, (numCorrect/float(total)*100))
print 'Incorrect:   %d %.1f%%'%(numIncorrect, (numIncorrect/float(total)*100))
print 'correct with pass', numCorrectWithPass

