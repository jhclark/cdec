#!/usr/bin/env bash
set -e
set -o pipefail
set -u

scriptDir=$(dirname $0)

if [[ $# != 8 ]]; then
    echo >&2 "Usage: $0 optWeightsIn graWeightsInPrefix weightsOut workDir iteration K MAX_BINS doReopt"
    echo >&2
    echo >&2 "Instead, I got: $@"
    exit 1
fi

set -x

# Weights from gradient descent (these microbins were seen in this sample)
optWeightsIn=$1
# In the grammar, we see all of the micro-binned real values that we could encounter
# Should be an absolute path
graWeightsInPrefix=$2
# Final weights (over all possible microbins) after macro-binning and smoothing
weightsOut=$3
workDir=$4
i=$5
K=$6
MAX_BINS=$7
doReopt=$8
#doDynMicroBin=$9 # TODO: Read this in...
#valuesPerMicrobin=${10}

im1=$(($i-1))

for feat in PTGS; do
    (echo '"feat","weight"'; egrep '^'"$feat" $optWeightsIn | cut -d@ -f2 | sort -gk1 -t' ' | sed 's/ /,/g') >| $workDir/weights.dist.$feat.$i
    $scriptDir/smooth_markov.py \
	< $workDir/weights.dist.$feat.$i \
	> $workDir/weights.smoothed.$feat.$i
    $scriptDir/binopt2.py $K $MAX_BINS \
	< $workDir/weights.smoothed.$feat.$i \
	> $workDir/weights.binned.$feat.$i
done

# Use other weights (not relevant to the dynamically binned feature)
# as an initialization point; this is especially important in the
# case of fixed features
egrep -v '^'$feat < $workDir/weights.opt.$i > $workDir/weights.reoptInit.$i

# TODO: format for weights file
feat=PTGS
if [[ "$doReopt" == 1 ]]; then
    # 1) Substitute binned features in mapoutput.* and weights.it
    cat $workDir/splag.$im1/mapoutput.* \
	| $scriptDir/splag_munge.py $workDir/weights.binned.$feat.$i $feat \
	> $workDir/splag.$im1/macrobinned.mapoutput

    # 2) Re-run optimizer on alternate inputs and write macro-bin weight
    PRO_OPTS=""
    cat $workDir/splag.$im1/macrobinned.mapoutput \
	| $scriptDir/../pro-train/mr_pro_reduce $PRO_OPTS -w $workDir/weights.reoptInit.$i \
	> $workDir/weights.reopt.$feat.$i

    # 3) Convert cdec format weights to our graphable format
    (echo '"feat","weight"'; egrep '^'"$feat" $workDir/weights.reopt.$feat.$i | cut -d@ -f2 | sort -gk1 -t' ' | sed 's/ /,/g') > $workDir/weights.reoptX.$feat.$i

    # 4) Project macro-bins back to microbins
    $scriptDir/macrobins_to_microbins.py $workDir/weights.reoptX.$feat.$i \
	< $graWeightsInPrefix.$feat \
        > $workDir/weights.macroX.$feat.$i
    awk -F, 'NR>1{print "'$feat'@"$1" "$2}' \
        < $workDir/weights.macroX.$feat.$i \
        > $workDir/weights.next.$feat.$i

else
    $scriptDir/macrobins_to_microbins.py $workDir/weights.binned.$feat.$i \
	< $graWeightsInPrefix.$feat \
        > $workDir/weights.macroX.$feat.$i
    awk -F, 'NR>1{print "'$feat'@"$1" "$2}' \
        < $workDir/weights.macroX.$feat.$i \
        > $workDir/weights.next.$feat.$i

fi

#if [[ "$doDynMicroBin" == "1" ]]; then
    # 1) Determine new bin boundaries (no longer 3 decimal places) and create new weights file
## $scriptDir/decide_microbins.py weights.macroX.$feat.$i $graWeightsInPrefix.$feat $valuesPerMicrobin \
## > $workDir/weights.microX.$feat.$i

    # 2) Re-format sent-level grammar and replace current one
## $scriptDir/reals_to_indicator.py --microbinFile $workDir/weights.microX.$feat.$i < CUR_GRAMMAR > NEW_GRAMMAR

# TODO: How do we replace the current sentence-level grammar directory? This will probably involve a call to GNU parallel...

#fi

(egrep -v '^'"$feat" $workDir/weights.reopt.$feat.$i; cat $workDir/weights.next.*.$i) > $weightsOut




# (for i in {0..1663}; do
#   echo >&2 $i
#   cat workdir.fbis.sa/gra-simple-prob-ind3/grammar.out.$i \
#     | awk -F' \\|\\|\\| ' '{print $4" "'$i'}' \
#     | uniq \
#     | sort -u -S4G; done) \
#  | sort -S4G > all-microbin3.probs