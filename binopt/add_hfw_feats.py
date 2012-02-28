#!/usr/bin/env python
import sys
import unicodedata
import codecs
import itertools

# Adds extra features:
# Nonterminal count
# Source/target terminal word counts (yes, we're duplicating target)
# Source/target high-frequency word count
# Source/target punctuation count
# Whether the source is identical to the target
# Optionally, some conjunctions thereof

(srcHfwsFile, tgtHfwsFile, useConjs) = sys.argv[1:]
useConjs = useConjs[0].lower() == 't'

sys.stdin = codecs.getreader('utf-8')(sys.stdin)
sys.stdout = codecs.getwriter('utf-8')(sys.stdout)
sys.stderr = codecs.getwriter('utf-8')(sys.stderr)

MAX_CONJS = 4

srcHfws = set([line.strip() for line in codecs.open(srcHfwsFile, 'r', 'utf-8')])
tgtHfws = set([line.strip() for line in codecs.open(tgtHfwsFile, 'r', 'utf-8')])

def isPunct(tok): return all([ unicodedata.category(c) == 'P' for c in tok])
def isNonterm(tok): return len(tok) >= 3 and tok[0] == '[' and tok[-1] == ']'
def escapeFeat(name): return name.replace(';','SEMI')

for line in sys.stdin:
    (lhs, src, tgt, feats, align) = line.strip().split(' ||| ')
    srcToks = src.split()
    tgtToks = tgt.split()
    
    newFeatList = []
    # We actually need count0 indicators in conjunctions to distinguish them from lower order conjunctions
    MIN = 0

    nontermCount = sum(isNonterm(tok) for tok in srcToks)
    if nontermCount > MIN: newFeatList.append("NontermCount_%d"%(nontermCount))

    srcTerms = [tok for tok in srcToks if not isNonterm(tok)]
    tgtTerms = [tok for tok in tgtToks if not isNonterm(tok)]

    newFeatList.append("SrcWC_%d"%(len(srcTerms)))
    newFeatList.append("TgtWC_%d"%(len(tgtTerms)))

    srcHfwCount = sum(tok in srcHfws and not isPunct(tok) for tok in srcTerms)
    tgtHfwCount = sum(tok in tgtHfws and not isPunct(tok) for tok in tgtTerms)
    if srcHfwCount > MIN: newFeatList.append("SrcHFWC_%d"%(srcHfwCount))
    if tgtHfwCount > MIN: newFeatList.append("TgtHFWC_%d"%(tgtHfwCount))

    srcPuncCount = sum(isPunct(tok) for tok in srcTerms)
    tgtPuncCount = sum(isPunct(tok) for tok in tgtTerms)

    srcLexFeats = ["SrcHFW_%s"%tok for tok in srcTerms if tok in srcHfws]
    tgtLexFeats = ["TgtHFW_%s"%tok for tok in tgtTerms if tok in tgtHfws]
    lexFeats = srcLexFeats + tgtLexFeats
    
    def conjoin(featList):
        result = []
        seedFeatList = list(featList)
        # Only do complex versions so that we don't have to worry about MIN
        #for r in range(2, min(len(seedFeatList), MAX_CONJS)+1):
        r = MAX_CONJS
        for combo in itertools.combinations(seedFeatList, r):
            result.append('_'.join(combo))
        return result
    
    if useConjs:
        #newFeatList += conjoin(newFeatList)
        if srcHfwCount > 0 or tgtHfwCount > 0:
            newFeatList.append("SrcHFWC_%d_TgtHFWC_%d"%(srcHfwCount, tgtHfwCount))
            newFeatList.append("NontermCount_%d_SrcHFWC_%d_TgtHFWC_%d"%(nontermCount, srcHfwCount, tgtHfwCount))
        if len(srcTerms) > 0 or len(tgtTerms) > 0:
            newFeatList.append("SrcWC_%d_TgtWC_%d"%(len(srcTerms), len(tgtTerms)))
            newFeatList.append("NontermCount_%d_SrcWC_%d_TgtWC_%d"%(nontermCount, len(srcTerms), len(tgtTerms)))
            if srcHfwCount > 0 or tgtHfwCount > 0:
                newFeatList.append("SrcWC_%d_TgtWC_%d_SrcHFWC_%d_TgtHFWC_%d"%(len(srcTerms), len(tgtTerms), srcHfwCount, tgtHfwCount))
                newFeatList.append("NontermCount_%d_SrcWC_%d_TgtWC_%d_SrcHFWC_%d_TgtHFWC_%d"%(nontermCount, len(srcTerms), len(tgtTerms), srcHfwCount, tgtHfwCount))

        for srcFeat in srcLexFeats:
            for tgtFeat in tgtLexFeats:
                lexFeats.append("%s_%s"%(srcFeat,tgtFeat))
                
    feats += ' ' + ' '.join([name+"=1" for name in newFeatList+lexFeats])
    feats = escapeFeat(feats)
    print ' ||| '.join([lhs, src, tgt, feats, align])
    

