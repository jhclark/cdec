#!/usr/bin/env python
import sys

# 1) Run MERT on whole tuning set (dist-vest.pl)
# 2) For each of n questions:
# 2a) Split error surfaces according to if source sentence passes question
# 2b) Aggregate error surfaces, find argmax, and accumulate error counts
# 2c) Find corpus-level aggregation and find aggregate score
# 2d) If gain is good enough, add node to decision tree
# 3) After asking all questions, write formatted decision tree model to disk for use by decoder
#    HACK version: just divide up sentences

class Question(object):
    # Eventually allow access to feature values
    # ...and even per-word feature values
    def ask(self, srcToks):
        return True

class QuestionQuestion(Question):
    def ask(self, srcToks):
        return srcToks[-1] == '?'

class LengthQuestion(Question):
    def __init__(self, n):
        self.n = n

    def ask(self, srcToks):
        return len(srcToks) >= self.n

class Node(object):
    def __init__(self):
        self.question = None
        self.yesBranch = None
        self.noBranch = None
        self.weights = None

def partition(errSurface, srcSents, q):
    yesBin = dict()
    noBin = dict()
    for sentId in errSurface:
        srcToks = srcSents[sentId]
        if q.ask(srcToks):
            yesBin[sentId] = errSurface[sentId]
        else:
            noBin[sentId] = errSurface[sentId]
    return (yesBin, noBin)

def optimize(questions, srcSents, errSurface, initWeights):
    # 1) evaluate corpus score
    score = evaluate(errSurface)

    # 2) ask questions
    for q in questions:
        (yesErr, noErr) = partition(errSurface, srcSents, q)

def loadWeights(f):
    w = dict()
    for line in open(f):
        (name, value) = line.strip().split()
        w[name] = float(value)
    return w

def loadSents(f):
    pass

def loadErrSurface(f):
    pass

if __name__ == '__main__':
    (srcSentFile, weightsFile, errSurfaceDir) = sys.argv[1:]

    questions = []
    questions.append(QuestionQuestion())
    questions.append(LengthQuestion(3))
    questions.append(LengthQuestion(5))
    questions.append(LengthQuestion(7))

    # Need initial weights
    dtree = Node()
    srcSents = loadSents(srcSentFile)
    initWeights = loadWeights(weightsFile)
    errSurface = loadErrSurface(f)
    optimize(questions, errSurface, initWeights)

