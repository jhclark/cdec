#!/usr/bin/awk -f

# Given the bin_info format produced by simple_binner.scala during bin induction,
# produce the graph regularizer format expected by mr_pro_reduce.cc for neighbor regularization:
#
# feat1 feat2 weight
#
# Note that edges are directed. 
# 'feat1 is penalized for being dissimilar from feat2 proportional to weight'

BEGIN {
#  population_weighting=0
  prevCount=0
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

  if (population_weighting) {
    total=count+prevCount
    weight_LR=count/total
    weight_RL=prevCount/total
  } else {
    weight_LR=0.5
    weight_RL=0.5
  }

  if (prevOrigFeat == origFeat && prevHighVal == lowVal) {
    # Note: edges in regularization graph are directed
    # TODO: Use neighbors?
    print prevBinFeat" "binFeat" "weight_LR
    print binFeat" "prevBinFeat" "weight_RL
  }

  prevOrigFeat=origFeat
  prevBinFeat=binFeat
  prevHighVal=highVal
  prevCount=count
}
