#!/usr/bin/env python
import sys
import codecs
from xml.sax.saxutils import escape

graPrefix = sys.argv[1]

sys.stdin = codecs.getreader("utf-8")(sys.stdin)
sys.stdout = codecs.getwriter("utf-8")(sys.stdout)

i = 0
for line in sys.stdin:
  print '<seg id="%d" grammar="%s%d"> '%(i,graPrefix,i) + escape(line.strip()) + " </seg>"
  i+=1
