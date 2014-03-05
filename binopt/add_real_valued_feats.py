#!/usr/bin/env python
import sys
import unicodedata
import codecs
import itertools
import argparse

parser = argparse.ArgumentParser(description='Add extra real-valued features before discretization')
parser.add_argument('--nontermFeats', help='Use non-terminal integer features', action='store_true')

### Phrase-level features ###
parser.add_argument('--srcHfwFile', help='Source high frequency words file')
parser.add_argument('--tgtHfwFile', help='Target high frequency words file')
parser.add_argument('--hfwCounts', type=int, help='Emit source/target counts of top-N high-frequency words, excluding punctuation (requires argument N)')
parser.add_argument('--minWordFreq', help='Adds the minimum word frequency of the phrase as a real-valued feature', action='store_true')
parser.add_argument('--maxWordFreq', help='Adds the maximum word frequency of the phrase as a real-valued feature', action='store_true')
parser.add_argument('--wordFreqs', help='Emit word frequencies for each word on source and target', action='store_true')

parser.add_argument('--phraseCharLen', help='Adds the length of the phrase in characters as an integer feature', action='store_true')
parser.add_argument('--puncCount', help='Adds the count of punctuation words as an integer feature', action='store_true')

args = parser.parse_args()

sys.stdin = codecs.getreader('utf-8')(sys.stdin)
sys.stdout = codecs.getwriter('utf-8')(sys.stdout)
sys.stderr = codecs.getwriter('utf-8')(sys.stderr)

if args.srcHfwFile:
    srcFreqs = dict()
    srcHfwList = []
    for line in codecs.open(args.srcHfwFile, 'r', 'utf-8'):
        (word, freq) = line.split()
        srcFreqs[word] = freq
        srcHfwList.append(word)
    srcHfwLexSet = set(srcHfwList[:args.srcHfwLex])

if args.tgtHfwFile:
    tgtFreqs = dict()
    tgtHfwList = []
    for line in codecs.open(args.tgtHfwFile, 'r', 'utf-8'):
        (word, freq) = line.split()
        tgtFreqs[word] = freq
        tgtHfwList.append(word)
    tgtHfwLexSet = set(tgtHfwList[:args.tgtHfwLex])

def isPunct(tok): return all([ unicodedata.category(c) == 'P' for c in tok ])
def isNonterm(tok): return len(tok) >= 3 and tok[0] == '[' and tok[-1] == ']'
def escapeFeat(name): return name.replace(';','SEMI')

for line in sys.stdin:
    while not line.endswith('\n'):
        line += sys.stdin.next() # don't break lines on special unicode markers
    (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
    srcToks = src.split()
    tgtToks = tgt.split()
    
    newFeatList = []

    nontermCount = sum(isNonterm(tok) for tok in srcToks)
    if args.nontermFeats:
        newFeatList.append("NontermCount_%d"%(nontermCount))

    srcTerms = [tok for tok in srcToks if not isNonterm(tok)]
    tgtTerms = [tok for tok in tgtToks if not isNonterm(tok)]

    if args.hfwCounts:
        srcHfwCount = sum( (tok in srcHfwCountSet and not isPunct(tok)) for tok in srcTerms)
        newFeatList.append("SrcHFWC_%d"%(srcHfwCount))
        tgtHfwCount = sum( (tok in tgtHfwCountSet and not isPunct(tok)) for tok in tgtTerms)
        newFeatList.append("TgtHFWC_%d"%(tgtHfwCount))

    if args.minWordFreq:
        c = min(srcFreq[tok] for tok in srcTerms)
        newFeatList.append("SrcMinWordFreq_%d"%(c))
        c = min(tgtFreq[tok] for tok in tgtTerms)
        newFeatList.append("TgtMinWordFreq_%d"%(c))

    if args.maxWordFreq:
        c = max(srcFreq[tok] for tok in srcTerms)
        newFeatList.append("SrcMaxWordFreq_%d"%(c))
        c = max(tgtFreq[tok] for tok in tgtTerms)
        newFeatList.append("TgtMaxWordFreq_%d"%(c))

    if args.wordFreqs:
        for tok in srcTerms:
            newFeatList.append("SrcWordFreq_%d"%(srcFreq[tok]))
        for tok in tgtTerms:
            newFeatList.append("TgtWordFreq_%d"%(tgtFreq[tok]))

    if args.phraseCharLen:
        srcChars = sum( len(tok) for tok in srcTerms )
        newFeatList.append("SrcRuleTermChars_%d"%(srcChars))
        tgtChars = sum( len(tok) for tok in tgtTerms )
        newFeatList.append("TgtRuleTermChars_%d"%(tgtChars))

    if args.puncCount:
        srcPuncCount = sum(isPunct(tok) for tok in srcTerms)
        newFeatList.append("SrcPuncTokCount_%d"%(srcPuncCount))
        tgtPuncCount = sum(isPunct(tok) for tok in tgtTerms)
        newFeatList.append("TgtPuncTokCount_%d"%(tgtPuncCount))

    feats += ' ' + ' '.join([name+"=1" for name in newFeatList])
    feats = escapeFeat(feats)
    print ' ||| '.join([lhs, src, tgt, feats, align])
    

