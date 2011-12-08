#!/usr/bin/env bash
set -e
set -o pipefail
set -u

scriptDir=$(dirname $0)

# Weights from gradient descent (these microbins were seen in this sample)
optWeightsIn=$1
# In the grammar, we see all of the micro-binned real values that we could encounter
graWeightsInPrefix=$2
# Final weights (over all possible microbins) after macro-binning and smoothing
weightsOut=$3
i=$4
K=$5
MAX_BINS=$6

for feat in PTGS; do
    (echo '"feat","weight"'; egrep '^'"$feat" $optWeightsIn | cut -d@ -f2 | sort -gk1 -t' ' | sed 's/ /,/g') >| weights.dist.$feat.$i
    $scriptDir/smooth_markov.py < weights.dist.$feat.$i > weights.smoothed.$feat.$i
    $scriptDir/binopt2.py $K $MAX_BINS < weights.smoothed.$feat.$i > weights.binned.$feat.$i
    $scriptDir/macrobins_to_microbins.py weights.binned.$feat.$i < $graWeightsInPrefix.$feat > weights.$feat.$i.next
    cat weights.$feat.$i.next | awk -F, 'NR>1{print "'$feat'@"$1" "$2}' > weights.$feat.$i.formatted.next
done

# TODO: format for weights file
feat=PTGS
(egrep -v '^'"$feat" $optWeightsIn; cat weights.*.$i.formatted.next) > $weightsOut



# (for i in {0..1663}; do
#   echo >&2 $i
#   cat workdir.fbis.sa/gra-simple-prob-ind3/grammar.out.$i \
#     | awk -F' \\|\\|\\| ' '{print $4" "'$i'}' \
#     | uniq \
#     | sort -u -S4G; done) \
#  | sort -S4G > all-microbin3.probs