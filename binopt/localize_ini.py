#!/usr/bin/env python
import sys
import shutil
import re
import os.path

localDir = sys.argv[1]

files = dict()

pat = re.compile(r'/[a-zA-Z0-9./_+-]+')

# 1) Find paths in the INI that need localizing
#    and print out new ini
for line in sys.stdin:
    for src in pat.findall(line):
        file = os.path.basename(src)
        dest = os.path.normpath(localDir + "/" + file)
        while dest in files.values():
            dest += ".1"
        files[src] = dest
        print >>sys.stderr, "Found {} => {}".format(src, dest)
        line = line.replace(src, dest)
    print line,

# 3) Copy files to local destination
for (src, dest) in files.iteritems():
    print >>sys.stderr, "Copying {} => {}".format(src, dest)
    shutil.copy(src, dest)
