#!/usr/bin/env perl

use strict;
use utf8;
#use 5.014; # require newer version

binmode(STDIN, ":utf8");
binmode(STDOUT, ":utf8");

while(<STDIN>) {
  chomp;
  my $line = $_;
  $line =~ s/(\p{Script=Hani})/ $1 /g;
  
#  $line =~ s/
#\p{Block=CJK_Compatibility}
#     |   \p{Block=CJK_Compatibility_Forms}
#     |   \p{Block=CJK_Compatibility_Ideographs}
#     |   \p{Block=CJK_Compatibility_Ideographs_Supplement}
#     |   \p{Block=CJK_Radicals_Supplement}
#     |   \p{Block=CJK_Strokes}
#     |   \p{Block=CJK_Symbols_And_Punctuation}
#     |   \p{Block=CJK_Unified_Ideographs}
#     |   \p{Block=CJK_Unified_Ideographs_Extension_A}
#     |   \p{Block=CJK_Unified_Ideographs_Extension_B}
#     |   \p{Block=CJK_Unified_Ideographs_Extension_C}
#    )
#  / $1 /g;
  $line =~ s/\s+/ /g;
  print "$line\n";
}

