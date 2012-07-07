#!/usr/bin/env bash
set -ueo pipefail

# This takes in cdec format grammars and produces a sorted list of features as:
# 

# Pipe all grammars on stdin
awk -F' \\|\\|\\| ' '{print $4}' \
    | awk 'BEGIN{RS=" "} {print}' \
    | awk -F= '{ if($0 != ""){feats[$1" "$2] += 1} } END{for(feat in feats) {print feat" "feats[feat]} }' \
    | sort -S4g -t' ' -k1,1 -k2,2g
