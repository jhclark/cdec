#!/usr/bin/env bash
set -e
set -o pipefail
set -u

scriptDir=$(dirname $0)

if [[ $# != 4 ]]; then
    echo >&2 "Usage: $0 tuneWorkDir tuneIter testGraWeightsInPrefix testWeightsOut"
    echo >&2
    echo >&2 "Instead, I got: $@"
    exit 1
fi

tuneWorkDir=$1
tuneIter=$2
# In the grammar, we see all of the micro-binned real values that we could encounter
# Should be an absolute path
testGraWeightsInPrefix=$3
# Final weights (over all possible microbins) after macro-binning and smoothing
testWeightsOut=$4

feat=PTGS
# 4) Project macro-bins back to microbins
$scriptDir/macrobins_to_microbins.py $tuneWorkDir/weights.reoptX.$feat.$tuneIter \
    < $testGraWeightsInPrefix.$feat \
    | awk -F, 'NR>1{print "'$feat'@"$1" "$2}' \
    > $testWeightsOut.$feat

reoptWeightsIn=$tuneWorkDir/weights.reopt.$feat.$tuneIter
(egrep -v '^'"$feat" $reoptWeightsIn; cat $testWeightsOut.$feat) > $testWeightsOut
