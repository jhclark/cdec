#!/usr/bin/perl -w
use strict;
use utf8;
use Unicode::Normalize;

binmode(STDIN, ":utf8");
binmode(STDOUT, ":utf8");

while(<STDIN>) {
    # TODO: There are many more specific rules that could be used here...
    $_ = NFKC($_);
    $_ = lc $_;
    print;
}

