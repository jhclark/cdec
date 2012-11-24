#!/usr/bin/env bash
set -ueo pipefail

# Converts an ARPA file into a real-valued "feature LM"
# usable as input to build_binary_jlm (when compiled with -DJLM_REAL_VALUES)
#
# Uses STDIN/STDOUT

awk -F$'\t' '/^-/{ if(NF==2) { print $2"\tLanguageModel="$1 } else { print $2"\tLanguageModel="$1"\tLanguageModel="$3 } }'
