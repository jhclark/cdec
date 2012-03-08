#!/usr/bin/env python
from __future__ import division
from collections import defaultdict

def zopen(filename):
    f = open(filename)
    if filename.endswith('.gz'):
        # Way faster than internal gzip (see http://stackoverflow.com/questions/8302911/python-equivalent-of-piping-file-output-to-gzip-in-perl-using-a-pipe)
        import subprocess
        p = subprocess.Popen("zcat " + filename, shell=True, stdout=subprocess.PIPE)
        return (p.stdout, f)
    else:
        return (f,f)

def bin(entries):
    eventCount = sum(entries.values())
    sortedEntries = sorted(entries.iteritems())
    # Do uniform binning, but make some adjustments based on
    # how many events we have left (since some very large categories with high frequency)
    # may span several bins
    remainingEvents = eventCount
    remainingBins = numBins
    eventsInBin = 0
    binStart = None
    bins = []
    for (i, (prob, freq)) in enumerate(sortedEntries):
        eventsInBin += freq
        if binStart == None:
            binStart = prob
        if eventsInBin > remainingEvents / remainingBins or i == len(sortedEntries) - 1:
            binEnd = prob
            bins.append( (binStart, binEnd) )
            #print "Have %d bins; remainingEvents %d remainingBins %d"%(len(bins), remainingEvents, remainingBins)
            #print binStart, binEnd
            
            binStart = None
            remainingBins -= 1
            remainingEvents -= eventsInBin
            eventsInBin = 0
            assert remainingEvents >= 0
            assert remainingBins >= 0
    return bins

def ngramIterator(lmFile):
    import os.path
    sz = os.path.getsize(lmFile)
    (f, fraw) = zopen(lmFile)
    n = "0"
    prevPct = 0.0
    for line in f:
        line = line.strip()
        if line.endswith('-grams:'):
            n = line[1]
            print >>sys.stderr, "Reading %s grams..."%n
        if line.startswith('-'):
            pct = fraw.tell() / sz
            if pct >= prevPct + 0.01:
                print >>sys.stderr, "%.0f%% through the file..."%(pct*100)
                prevPct = pct
            cols = line.split('\t')
            if len(cols) == 3:
                (logProb, ngram, backoff) = cols
                yield (n, logProb, ngram, backoff)
            else:
                (logProb, ngram) = cols
                yield (n, logProb, ngram, None)
    f.close()

def findBins(lmFile, numBins):

    def dump(probs, backoffs, n):
        probBins[n] = bin(probs)
        backoffBins[n] = bin(backoffs)
        probs = defaultdict(lambda: 0)
        backoffs = defaultdict(lambda: 0)
        print >>sys.stderr, "Prob bins",n,probBins[n]
        print >>sys.stderr, "Backoff bins",n,backoffBins[n]

    probs = defaultdict(lambda: 0)
    backoffs = defaultdict(lambda: 0)
    probBins = dict() # order -> binList
    backoffBins = dict() # order -> binList

    prevN = "0"
    for (n, logProb, ngram, backoff) in ngramIterator(lmFile):
        if n != prevN and prevN != "0":
            dump(probs, backoffs, prevN)
        prevN = n
        probs[float(logProb)] += 1
        if backoff != None:
            backoffs[float(backoff)] += 1
    dump(probs, backoffs, n)

    return (probBins, backoffBins)

def quantLM(lmFile, probBins, backoffBins):
    def findBin(bins, val):
        for (i, (start, end)) in enumerate(bins):
            if val <= end:
                return i
        raise Exception("No matching bin found for %f in %s"%(val, str(bins)))

    def addXOS(featName, ngram):
        if ngram.startswith("<s>"):
            featName += "_BOS"
        if ngram.endswith("</s>"):
            featName += "_EOS"
        return featName

    hasUnk = False
    for (n, logProb, ngram, backoff) in ngramIterator(lmFile):
        iLogProb = findBin(probBins[n], float(logProb))
        probFeat = addXOS("LM_Match_Len"+n+"_Bin"+str(iLogProb), ngram)
        if backoff == None:
            print '\t'.join((ngram, probFeat))
        else:
            iBackoff = findBin(backoffBins[n], float(backoff))
            backoffFeat = addXOS("LM_Miss_Len"+n+"_Bin"+str(iBackoff), ngram)
            print '\t'.join((ngram, probFeat, backoffFeat))
        if ngram == "<unk>":
            hasUnk = True
    if not hasUnk:
        print '\t'.join(("<unk>", "LM_UNK"))

if __name__ == '__main__':
    import sys
    (lmFile, numBins) = sys.argv[1:]
    numBins = int(numBins) # 16

    (probBins, backoffBins) = findBins(lmFile, numBins)
    quantLM(lmFile, probBins, backoffBins)
