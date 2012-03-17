#!/usr/bin/env python
import sys
import unicodedata
import codecs
import itertools
import argparse

# Adds extra features:
# Nonterminal count
# Source/target terminal word counts (yes, we're duplicating target)
# Source/target high-frequency word count
# Source/target punctuation count
# Whether the source is identical to the target
# Optionally, some conjunctions thereof

# NOTE: Conjunctions can have incomplete information if the initial feature set has N degrees of freedom,
#       then the original feature set only needed N-1 features to represent it (i.e. zero features could
#       be dropped).

parser = argparse.ArgumentParser(description='Add extra features such as High Frequency Word Counts')
#parser.add_argument('srcHfwFile', metavar='N', type=int, nargs='+', help='Source high frequency words file')
parser.add_argument('--lenFeats', help='Use length features', action='store_true')
parser.add_argument('--nontermFeats', help='Use non-terminal features', action='store_true')
parser.add_argument('--srcHfwFile', help='Source high frequency words file')
parser.add_argument('--tgtHfwFile', help='Target high frequency words file')
parser.add_argument('--srcHfwCounts', help='Use source high frequency word count features using all words in the HFW words file (requires filename)', action='store_true')
parser.add_argument('--tgtHfwCounts', help='Use target high frequency word count features using all words in the HFW words file (requires filename)', action='store_true')
parser.add_argument('--srcHfwLex', type=int, help='Use source high frequency word lexical features, using the top N words (requires filename)')
parser.add_argument('--tgtHfwLex', type=int, help='Use target high frequency word lexical features, using the top N words (requires filename)')
parser.add_argument('--conjoin', type=int, help='Use conjunctions of length N (recommended: 4)', default=0)

args = parser.parse_args()


sys.stdin = codecs.getreader('utf-8')(sys.stdin)
sys.stdout = codecs.getwriter('utf-8')(sys.stdout)
sys.stderr = codecs.getwriter('utf-8')(sys.stderr)

srcHfwList = [line.strip() for line in codecs.open(args.srcHfwFile, 'r', 'utf-8')]
tgtHfwList = [line.strip() for line in codecs.open(args.tgtHfwFile, 'r', 'utf-8')]
# Use whole file for counts
srcHfwCountSet = set(srcHfwList)
tgtHfwCountSet = set(tgtHfwList)
# Use subset for lexical features
srcHfwLexSet = set(srcHfwList[:args.srcHfwLex])
tgtHfwLexSet = set(tgtHfwList[:args.tgtHfwLex])


def isPunct(tok): return all([ unicodedata.category(c) == 'P' for c in tok])
def isNonterm(tok): return len(tok) >= 3 and tok[0] == '[' and tok[-1] == ']'
def escapeFeat(name): return name.replace(';','SEMI')

for line in sys.stdin:
    while not line.endswith('\n'):
        line += sys.stdin.next() # don't break lines on special unicode markers
    (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
    srcToks = src.split()
    tgtToks = tgt.split()
    
    newFeatList = []
    # We actually need count0 indicators in conjunctions to distinguish them from lower order conjunctions
    MIN = 0

    nontermCount = sum(isNonterm(tok) for tok in srcToks)
    if args.nontermFeats and nontermCount > MIN:
        newFeatList.append("NontermCount_%d"%(nontermCount))

    srcTerms = [tok for tok in srcToks if not isNonterm(tok)]
    tgtTerms = [tok for tok in tgtToks if not isNonterm(tok)]

    if args.lenFeats:
        newFeatList.append("SrcWC_%d"%(len(srcTerms)))
        newFeatList.append("TgtWC_%d"%(len(tgtTerms)))

    srcHfwCount = sum(tok in srcHfwCountSet and not isPunct(tok) for tok in srcTerms)
    tgtHfwCount = sum(tok in tgtHfwCountSet and not isPunct(tok) for tok in tgtTerms)
    if args.srcHfwCounts:
        if srcHfwCount > MIN: newFeatList.append("SrcHFWC_%d"%(srcHfwCount))
    if args.tgtHfwCounts:
        if tgtHfwCount > MIN: newFeatList.append("TgtHFWC_%d"%(tgtHfwCount))

    srcPuncCount = sum(isPunct(tok) for tok in srcTerms)
    tgtPuncCount = sum(isPunct(tok) for tok in tgtTerms)

    srcLexFeats = []
    tgtLexFeats = []
    if args.srcHfwLex:
        srcLexFeats = ["SrcHFW_%s"%tok for tok in srcTerms if tok in srcHfwLexSet]
    if args.tgtHfwLex:
        tgtLexFeats = ["TgtHFW_%s"%tok for tok in tgtTerms if tok in tgtHfwLexSet]
    lexFeats = srcLexFeats + tgtLexFeats

    # XXX: Unused
    def conjoin(featList):
        result = []
        seedFeatList = list(featList)
        # Only do complex versions so that we don't have to worry about MIN
        #for r in range(2, min(len(seedFeatList), MAX_CONJS)+1):
        r = args.conjoin
        for combo in itertools.combinations(seedFeatList, r):
            result.append('_'.join(combo))
        return result
    
    if args.conjoin >= 2:
        #newFeatList += conjoin(newFeatList)
        if srcHfwCount > 0 or tgtHfwCount > 0:
            if args.srcHfwCounts and args.tgtHfwCounts:
                newFeatList.append("SrcHFWC_%d_TgtHFWC_%d"%(srcHfwCount, tgtHfwCount))
                if args.conjoin >= 3 and args.nontermFeats:
                    newFeatList.append("NontermCount_%d_SrcHFWC_%d_TgtHFWC_%d"%(nontermCount, srcHfwCount, tgtHfwCount))
        if len(srcTerms) > 0 or len(tgtTerms) > 0:
            if args.lenFeats:
                newFeatList.append("SrcWC_%d_TgtWC_%d"%(len(srcTerms), len(tgtTerms)))
                if args.conjoin >= 3 and args.nontermFeats:
                    newFeatList.append("NontermCount_%d_SrcWC_%d_TgtWC_%d"%(nontermCount, len(srcTerms), len(tgtTerms)))
                if srcHfwCount > 0 or tgtHfwCount > 0:
                    if args.conjoin >= 4 and args.srcHfwCounts and args.tgtHfwCounts:
                        newFeatList.append("SrcWC_%d_TgtWC_%d_SrcHFWC_%d_TgtHFWC_%d"%(len(srcTerms), len(tgtTerms), srcHfwCount, tgtHfwCount))
                        if args.conjoin >= 5 and args.nontermFeats:
                            newFeatList.append("NontermCount_%d_SrcWC_%d_TgtWC_%d_SrcHFWC_%d_TgtHFWC_%d"%(nontermCount, len(srcTerms), len(tgtTerms), srcHfwCount, tgtHfwCount))

        if args.srcHfwLex and args.tgtHfwLex:
            for srcFeat in srcLexFeats:
                for tgtFeat in tgtLexFeats:
                    lexFeats.append("%s_%s"%(srcFeat,tgtFeat))
                
    feats += ' ' + ' '.join([name+"=1" for name in newFeatList+lexFeats])
    feats = escapeFeat(feats)
    print ' ||| '.join([lhs, src, tgt, feats, align])
    

