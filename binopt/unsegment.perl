#!/usr/bin/perl -w

use strict;

binmode(STDIN, ":utf8");
binmode(STDOUT, ":utf8");

while(<STDIN>) {
  my $line = $_;
  $line =~ s/ \+[^ ]+//g; # for example: word +suffix
  $line =~ s/[^ ]+\# //g; # for example: prefix# word
  print $line;
}
