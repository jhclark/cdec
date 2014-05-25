#!/usr/bin/gawk -f

BEGIN {
    RS=" "
}

/=/ {
  match($0, /([^ =]+)=([0-9.e-]+)/, matches)
  feats[matches[1]]=1
}

END {
  i=1
  for (feat in feats) {
      print i" "feat
      i++
  }
}
