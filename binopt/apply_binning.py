#!/usr/bin/env python
import sys
import math
from collections import defaultdict
from operator import itemgetter
from bisect import *

(bin_info_file, mode) = sys.argv[1:3]

def die(msg):
  print >>sys.stderr, msg
  sys.exit(1)

def has_opt(flag): return flag in sys.argv[3:]
def get_opt_arg(flag):
  for i in range(3, len(sys.argv)):
    if sys.argv[i] == flag:
      if i+1 == len(sys.argv): die("ERROR: Expected argument for option " + flag)
      return sys.argv[i+1]
  return None

keep_orig_feats = has_opt('--keep-orig-feats')
# When discretizing only a subset of the features,
# Allow passing through other features -- normally, this should
# be disabled, since failing on unrecognized features can help detect buts
allow_unrecognized_feats = has_opt('--allow-unrecognized-feats')

# the name of the single feature that we'll be binning
# value is None if "--bin-single-feat FeatureName" wasn't passed
# This implies that all other features besides this feature will be kept in their original form
bin_single_feat = get_opt_arg('--bin-single-feat')
if bin_single_feat != None: print >>sys.stderr, "Binning single feature:", bin_single_feat

if mode == 'indicator':
  print >>sys.stderr, "Using indicator bin features"
  useIndicators = True
elif mode == 'real':
  print >>sys.stderr, "Using real-valued bin features"
  useIndicators = False
else:
  die("Unrecognized mode: " + mode)

# featName -> [ (destFeatName, lowVal, highVal) ]
# destFeatName fires iff lowVal < featVal <= highVal
# -inf and inf are legal bounds
# multiple destFeats may fire for a single original feature
# original features are removed after binning

# Incoming lines look like this:
# bin MaxLexEGivenF MaxLexEGivenF_-0.000000_0.916822_Overlap0 -inf <= x < 0.916942 [ -0.000000 - 0.916822 count=48950 adjCount=48950 L=35 R=70 ]
print >>sys.stderr, "Reading bin info..."
tmp_bin_info = defaultdict(list)
f = open(bin_info_file)
for line in f:
  cols = line.strip().split(' ')
  featName = cols[1]
  destFeatName = cols[2]
  lowValue = float(cols[3])
  highValue = float(cols[7])
  tmp_bin_info[featName].append( (lowValue, highValue, destFeatName) )
f.close()

# REMEMBER: There could be overlapping bins!
print >>sys.stderr, "Sorting and indexing bin info..."
all_bin_info = dict()
for (key,values) in tmp_bin_info.iteritems():
  values.sort(key=itemgetter(0,1))
  # Create a low value index for the bisect method
  idx = [ lowValue for (lowValue, highValue, destFeatName) in values ]
  all_bin_info[key] = (idx, values)
tmp_bin_info = None

# See python "bisect" documentation
def find_lt(values, value):
    'Find leftmost index whose value is < item'
    i = bisect_left(values, value)
    if i:
        return i-1
    else:
      raise ValueError

for line in sys.stdin:
  #print "XXX ",line,
  (lhs, src, tgt, feats, align) = line.strip("\n").split(' ||| ')
  featList = [x.split('=') for x in feats.split()]

  # Remove log transform from all sa-align features:
  # MaxLexEGivenF
  # EGivenFCoherent: -log10(PairCount / SampleCount) (sa-extract/context_model.py:100)
  # MaxLexFGivenE
  result = []
  for (name, strValue) in featList:
    value = float(strValue)
    
    if keep_orig_feats:
      result.append(name + "=" + strValue)

    if bin_single_feat != None and bin_single_feat != name:
      # we're only binning one feature, and this isn't it
      if not keep_orig_feats:
        result.append(name + "=" + strValue)
    elif bin_single_feat == None or bin_single_feat == name:
      # either we're binning all features or this is the single feature we're binning
      try:
        (my_bin_idx, my_bin_info) = all_bin_info[name]

      # First, do a binary seach to get in the right neighborhood
        try:
          start_idx = find_lt(my_bin_idx, value)
        except:
          print >>sys.stderr, "Feature value {} not found in index: {}".format(value, my_bin_idx)
          raise

        # Then iterate over the contents to find out which bins apply
        # *starting* at i and terminating early as soon as we exceed high_value
        found = 0
        for (lowValue, highValue, destFeatName) in my_bin_info[start_idx:]:
          if lowValue <= value:
            if value < highValue:
              destValue = "1" if useIndicators else strValue
              result.append(destFeatName + "=" + destValue)
              found += 1
          else:
            # Terminate early since values are sorted by (lowValue, highValue)
            # REMEMBER: There could be overlapping bins!
            break
      except KeyError:
        if not allow_unrecognized_feats:
          die("Unrecognized feature: " + name)
      if not allow_unrecognized_feats and found == 0:
        die("Found zero bins for feature: " + name)
  feats = ' '.join(result)
  print ' ||| '.join([lhs, src, tgt, feats, align])
