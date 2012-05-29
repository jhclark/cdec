#!/usr/bin/env python
import sys

refFiles = sys.argv[1:]

refs = []
for refFile in refFiles:
  f = open(refFile)
  refs.append(list(f.readlines()))
  f.close()

for j in range(len(refs[0])):
  for i in range(len(refs)):
    print refs[i][j],
