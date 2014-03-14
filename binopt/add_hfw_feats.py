#!/usr/bin/env python
import sys
import unicodedata
import codecs
import itertools
import argparse

# Adds extra features to an **already discretized** grammar
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

parser.add_argument('--srcHfwFile', help='Source high frequency words file')
parser.add_argument('--tgtHfwFile', help='Target high frequency words file')
parser.add_argument('--hfwLex', type=int, help='Use source/target high frequency word lexical indicator features, using the top N words (requires argument N)')
# Counts will get added and discretized separately.

parser.add_argument('--srcTermFeats', help='Use source terminal count (phrase length) integer features (number of terminals)', action='store_true')
parser.add_argument('--tgtTermFeats', help='Use source terminal count (phrase length) integer features (number of terminals)', action='store_true')

### Rule-level features ###
parser.add_argument('--srcBrownPaths', help='Source brown clusters paths file (cluster, word, freq)')
parser.add_argument('--tgtBrownPaths', help='Target brown clusters paths file (cluster, word, freq)')
parser.add_argument('--brownUnigrams', help='Emit brown unigram indicators', action='store_true') ###

### Alignment-level conjunctions ###
parser.add_argument('--brownAligned', help='Emit brown indicators for aligned word pairs', action='store_true') ###
parser.add_argument('--puncAligned', help='Emit punctuation indicators for aligned word pairs', action='store_true') ###
### How do we add discretized word frequencies as conjunctions now? Read in the discretization file here? Make the conjunction script more complicated?
### How do we add discretized word character counts here?
### #prefix and +suffix rules for arabic? or will this fall out from frequency features?

### Word-level features ###
# We add a hypothetical IsLeftmost and IsRightmost word-level features (for brown and word-freq features), but these are guaranteed to have no signal at the phrase level
# IsAligned (left-unaligned, right-unaligned)
# Conjoin by position from left (1,2,3),and from right (-1, -2, -3)

args = parser.parse_args()

sys.stdin = codecs.getreader('utf-8')(sys.stdin)
sys.stdout = codecs.getwriter('utf-8')(sys.stdout)
sys.stderr = codecs.getwriter('utf-8')(sys.stderr)

if args.srcHfwFile:
    srcHfwList = []
    for line in open(args.srcHfwFile, 'r'):
        (word, freq) = line.decode('utf-8').strip().split()
        srcHfwList.append(word)
    srcHfwLexSet = set(srcHfwList[:args.srcHfwLex])

if args.tgtHfwFile:
    tgtHfwList = []
    for line in open(args.tgytHfwFile, 'r'):
        (word, freq) = line.decode('utf-8').strip().split()
        tgytHfwList.append(word)
    tgytHfwLexSet = set(tgytHfwList[:args.tgytHfwLex])

if args.srcBrownPaths:
    srcBrown = dict()
    i = 0
    for line in open(args.srcBrownPaths, 'r'):
        cols = line.decode('utf-8').strip().split('\t')
        (cluster, word, xx) = cols
        srcBrown[word] = cluster
        i += 1

if args.tgtBrownPaths:
    tgtBrown = dict()
    for line in open(args.tgtBrownPaths, 'r'):
        (cluster, word, xx) = line.decode('utf-8').strip().split('\t')
        tgtBrown[word] = cluster

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

    if len(srcToks) == 0 or len(tgtToks) == 0:
        print >>sys.stderr, "IGNORING BROKEN LINE:",line.strip()
        continue

    srcTerms = [tok for tok in srcToks if not isNonterm(tok)]
    tgtTerms = [tok for tok in tgtToks if not isNonterm(tok)]

    if args.brownUnigrams:
        for tok in srcTerms:
            cluster = srcBrown[tok]
            newFeatList.append("SrcBrown_%s"%(cluster))
        for tok in tgtTerms:
            cluster = tgtBrown[tok]
            newFeatList.append("TgtBrown_%s"%(cluster))

    # Even though these would typically be integer features
    # and then discretized, we add them here as indicators because
    # 1) These's few enough of them that they would always receive separate bins
    # 2) The src features are guaranteed to do nothing without conjunctions
    # 3) The target features are equivalent to the word penalty without discretization
    if args.srcTermFeats:
        newFeatList.append("SrcTermCount_%d"%(len(srcTerms)))
    if args.tgtTermFeats:
        newFeatList.append("TgtTermCount_%d"%(len(tgtTerms)))

    for link in align.split():
        link = link.strip()
        if not link:
            continue
        (i, j) = link.split('-')
        i = int(i)
        j = int(j)
        try:
            srcWord = srcToks[i]
            tgtWord = tgtToks[j]
        except:
            print >>sys.stderr, "ERROR in line:",line.strip()
            raise
        if args.brownAligned:
            srcCluster = srcBrown[srcWord]
            tgtCluster = tgtBrown[tgtWord]
            newFeatList.append("BrownAligned_%s_%s"%(srcCluster, tgtCluster))
        if args.puncAligned:
            srcPunc = "SrcPunc" if isPunct(srcWord) else "NotSrcPunc"
            tgtPunc = "TgtPunc" if isPunct(tgtWord) else "NotTgtPunc"
            newFeatList.append("%s_Aligned_%s"%(srcPunc, tgtPunc))

    srcLexFeats = []
    tgtLexFeats = []
    if args.hfwLex:
        srcLexFeats = ["SrcHFW_%s"%tok for tok in srcTerms if tok in srcHfwLexSet]
        tgtLexFeats = ["TgtHFW_%s"%tok for tok in tgtTerms if tok in tgtHfwLexSet]
    lexFeats = srcLexFeats + tgtLexFeats
                
    feats += ' ' + ' '.join([name+"=1" for name in newFeatList+lexFeats])
    feats = escapeFeat(feats)
    print ' ||| '.join([lhs, src, tgt, feats, align])
    

