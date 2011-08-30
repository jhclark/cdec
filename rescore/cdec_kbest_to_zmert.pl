#!/usr/bin/perl -w

use strict;
use utf8;
use Getopt::Long;

my $feature_file;
my $hyp_file;
my $help;
my $n;
my $pad;

Getopt::Long::Configure("no_auto_abbrev");
if (GetOptions(
    "feature_file|f=s" => \$feature_file,
    "hypothesis_file|h=s" => \$hyp_file,
    "nbest_size|n=s" => \$n,
    "pad" => \$pad,
    "help" => \$help,
) == 0 || @ARGV!=0 || $help || !$feature_file || !$hyp_file || ($pad && !$n)) {
  usage();
  exit(1);
}

open W, "<$feature_file" or die "Can't read $feature_file: $!";
my %weights;
my @all_feats;
while(<W>) {
  chomp;
  next if /^#/;
  next if /^\s*$/;
  my ($fname, $w) = split /\s+/;
  push @all_feats, $fname;
  $weights{$fname} = 1;
}
close W;

open HYP, "<$hyp_file" or die "Can't read $hyp_file: $!";
my $previd = 0;
my $hypcount = 0;
my ($id, $hyp, $feats);
my @trans;
while(<HYP>) {
  chomp;
  ($id, $hyp, $feats) = split / \|\|\| /;
  
  if ($previd != $id) {
      while($pad && $hypcount < $n) {
	  print "$previd ||| $hyp ||| @trans\n";
	  $hypcount++;
      }
      $hypcount = 0;
      $previd = $id;
  }

  my @afeats = split /\s+/, $feats;
  my $tot = 0;
  my %fvaldict;
  for my $featpair (@afeats) {
    my ($fname,$fval) = split /=/, $featpair;
    $fvaldict{$fname} = $fval;
    my $weight = $weights{$fname};
    warn "Feature '$fname' not mentioned in feature file $feature_file" unless defined $weight;
    $weights{$fname} = 1;
  }

  @trans = ();
  for my $feat (@all_feats) {
    my $v = $fvaldict{$feat};
    if (!defined $v) { $v = '0.0'; }
    push @trans, $v;
  }
  print "$id ||| $hyp ||| @trans\n";
  $hypcount++;
}
while($pad && $hypcount < $n) {
    print "$previd ||| $hyp ||| @trans\n";
    $hypcount++;
}

close HYP;

sub usage {
  print <<EOT;
Usage: $0 -f feature-file.txt/weights.txt -h hyp.nbest.txt
  Puts a cdec k-best list into Joshua/ZMERT format
EOT
}

