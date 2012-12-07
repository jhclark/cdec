#!/usr/bin/env python
import sys
import codecs
import os
import os.path
from xml.sax.saxutils import escape
import gzip

def zopen(filename, mode='r'):
  if filename.endswith('.gz'):
    return gzip.open(filename, mode)
  else:
    return open(filename, mode)

graPrefix = sys.argv[1]

# Second argument can be a file with observable sentence-level features,
# one set of features per line (parallel with source sentences). Features are space-delimited indicator features.
obsFeatsFile = None
obsFeatsFilename = None
lenFile = None
lenFilename = None

opts = sys.argv[2:]
if not opts[0].startswith("--"):
  raise Exception("Invalid option: {}".format(opts[0]))
i = 0
while i < len(opts):
  opt = opts[i]
  if opt == '--obsFeatsFile':
    i += 1
    val = opts[i]
    obsFeatsFilename = val
  elif opt == '--lengthFile':
    i += 1
    val = opts[i]
    lenFilename = val
  else:
    raise Exception("Unknown option: {}".format(opt))
  i += 1

if obsFeatsFilename:
  obsFeatsFile = zopen(obsFeatsFilename)
if lenFilename:
  lenFile = zopen(lenFilename)

sys.stdin = codecs.getreader("utf-8")(sys.stdin)
sys.stdout = codecs.getwriter("utf-8")(sys.stdout)

i = 0
for line in sys.stdin:
  filename = "%s%d"%(graPrefix,i)
  if not os.path.exists(filename):
    filenameGz = filename + ".gz"
    if not os.path.exists(filenameGz):
      print >>sys.stderr, "Grammar file not found: ", filename, filenameGz
      sys.exit(1)
    else:
      filename = filenameGz
  
  attribs = []
  attribs.add('id="{}"'.format(i))
  attribs.add('grammar="{}"'.format(filename))
  if obsFeatsFile:
    obsFeats = obsFeatsFile.next().strip()
    attribs.add('features="{}"'.format(obsFeats))
  if lenFile:
    L = lenFile.next().strip()
    attribs.add('desired_len="{}"'.format(L))
    
  print '<seg {}> '.format(' '.join(attribs)) + escape(line.strip()) + " </seg>"
  i+=1

