#!/usr/bin/awk -f

# Given the bin_info format produced by simple_binner.scala during bin induction,
# produce the graph regularizer format expected by mr_pro_reduce.cc for neighbor regularization:
#
# feat1 feat2 weight
#
# Note that edges are directed. 

/^bin/ {
  origFeat=$2
  binFeat=$3
  lowVal=$4
  highVal=$8

  if (prevOrigFeat == origFeat && prevHighVal == lowVal) {
    # Note: edges in regularization graph are directed
    print prevBinFeat" "binFeat" 0.5"
    print binFeat" "prevBinFeat" 0.5"
  }

  prevOrigFeat=origFeat
  prevBinFeat=binFeat
  prevHighVal=highVal
}