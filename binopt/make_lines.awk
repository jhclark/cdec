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
  writeConjunctions=1

  curLineId=0
  curFeatId=0
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
      curLineId += 1
      curFeatId = 1
  }

  featArray[curLineId,curFeatId] = binFeat
  featArrayLens[curLineId] += 1
  curFeatId += 1

  prevOrigFeat=origFeat
  prevBinFeat=binFeat
  prevHighVal=highVal
  prevCount=count
}

# Print all of the features
END {
    for (i = 1; i <= curLineId; i++) {
        printf C" "windowSize
        for (j = 1; j <= featArrayLens[i]; j++) {
            binFeat = featArray[i,j]
            printf " "binFeat	    
        }
        printf "\n"
    }

    if (writeConjunctions) {
        for (i = 1; i <= curLineId; i++) {
            for (j = 1; j <= featArrayLens[i]; j++) {
                binFeat1 = featArray[i,j]
                for (ii = 1; ii < i; ii++) {
                    printf C" "windowSize
                    for (jj = 1; jj <= featArrayLens[ii]; jj++) {
                        binFeat2 = featArray[ii,jj]
                        printf " "binFeat1"__"binFeat2
                    }
                    printf "\n"
                }
            }
	}
    }
}
