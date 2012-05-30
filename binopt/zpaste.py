#!/usr/bin/env python
import sys
import gzip

def zopen(filename, mode='r'):
  if filename.endswith('.gz'):
    return gzip.open(filename, mode)
  else:
    return open(filename, mode)

filenames = sys.argv[1:]
files = [ zopen(f) for f in filenames ]

# TODO: Err on non-parallel files?
while True:
  for f in files:
    lineArr = [ f.next().strip() for f in files ]
    print '\t'.join(lineArr)

for f in files:
  f.close()
