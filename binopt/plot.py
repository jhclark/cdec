#!/usr/bin/env python
import matplotlib
from matplotlib.backends.backend_agg import FigureCanvasAgg as FigureCanvas
from matplotlib.figure import Figure
import matplotlib.mlab as mlab
from matplotlib import pylab
import numpy as np

import sys
args = sys.argv[1:]
if len(args) == 2:
    (inFile, outFile) = args
    inFileBinned = None
else:
    (inFile, inFileBinned, outFile) = args

pylab.title("Feature versus weight",fontsize=14)
pylab.xlabel("Indicator Feature (Value in probability space)",fontsize=12)
pylab.ylabel("Weight",fontsize=12)
pylab.grid(True,linestyle='-',color='0.75')
pylab.ylim([-1,1])

#pylab.scatter(r.feat,r.weight,s=20,color='tomato');
r = mlab.csv2rec(inFile)
pylab.plot(r.feat, r.weight, 'k-', linewidth=2)

if inFileBinned:
    r = mlab.csv2rec(inFileBinned)
    pylab.plot(r.feat, r.weight, 'k-', linewidth=2, color='red')

#pylab.show()
pylab.savefig(outFile,dpi=500)
