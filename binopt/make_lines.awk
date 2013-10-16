#!/usr/bin/awk -f

# Given the bin_info format produced by simple_binner.scala during bin induction,
# produce the feature "line groups" needed by tangent / power regularization
#
# strength window_size feat1 [feat2 feat3...]

BEGIN {
  prevCount=0
  first=1
# Use a *very* strong regularization penalty on this one since we should be very precisely penalizing bad things
  #C=5000000
  windowSize=1
}

/^bin/ {
  origFeat=$2
  binFeat=$3
  lowVal=$4
  highVal=$8
  if (NF == 15) {
    count=$11
  } else if (NF == 17) {
    count=$13
  } else {
    exit(1)
  }
  # Count field looks like 'count=123'
  count=gensub(/count=/, "", "g", count)

  # TODO: Detect new feature groups
  if (prevOrigFeat != origFeat) {
      if (first) {
	  first=0
      } else {
	  printf "\n"
      }
      printf C" "windowSize
  }

  if (prevOrigFeat == origFeat && prevHighVal == lowVal) {
    printf " "binFeat
  }

  prevOrigFeat=origFeat
  prevBinFeat=binFeat
  prevHighVal=highVal
  prevCount=count
}
