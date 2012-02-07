#!/usr/bin/perl -w

# Taken from Moses' train-model.perl by Philipp Koehn
# LGPL

use strict;
use Getopt::Long "GetOptions";
use FindBin qw($Bin);
use File::Spec::Functions;
use File::Basename;

# From Train Factored Phrase Model
# (c) 2006-2009 Philipp Koehn
# with contributions from other JHU WS participants
# Train a model from a parallel corpus
# -----------------------------------------------------
$ENV{"LC_ALL"} = "C";

# utilities
my $ZCAT = "gzip -cd";
my $BZCAT = "bzcat";

my ($_F, $_E, $_A, $_F2E, $_E2F) = ("","","","","");
GetOptions('f=s' => \$_F,
	   'e=s' => \$_E,
	   'a=s' => \$_A,
	   'f2e=s' => \$_F2E,
	   'e2f=s' => \$_E2F);

if($_F eq "") {
    print STDERR "ERROR: f not specified";
  exit(1);
}

get_lexical($_F, $_E, $_A, $_F2E, $_E2F);

sub get_lexical {
    my ($alignment_file_f,$alignment_file_e,$alignment_file_a,$f2e,$e2f) = @_;
    print STDERR "($alignment_file_f,$alignment_file_e,$alignment_file_a,$f2e,$e2f)\n";
    #my $alignment_file_a = $___ALIGNMENT_FILE.".".$___ALIGNMENT;

    my (%WORD_TRANSLATION,%TOTAL_FOREIGN,%TOTAL_ENGLISH);

    #if (-e "$lexical_file.f2e" && -e "$lexical_file.e2f") {
    #  print STDERR "  reusing: $lexical_file.f2e and $lexical_file.e2f\n";
    #  return;
    #}

    open(E,&open_compressed($alignment_file_e)) or die "ERROR: Can't read $alignment_file_e";
    open(F,&open_compressed($alignment_file_f)) or die "ERROR: Can't read $alignment_file_f";
    open(A,&open_compressed($alignment_file_a)) or die "ERROR: Can't read $alignment_file_a";

    my $alignment_id = 0;
    while(my $e = <E>) {
        if (($alignment_id++ % 1000) == 0) { print STDERR "!"; }
        chomp($e); fix_spaces(\$e);
        my @ENGLISH = split(/ /,$e);
        my $f = <F>; chomp($f); fix_spaces(\$f);
        my @FOREIGN = split(/ /,$f);
        my $a = <A>; chomp($a); fix_spaces(\$a);

        my (%FOREIGN_ALIGNED,%ENGLISH_ALIGNED);
        foreach (split(/ /,$a)) {
            my ($fi,$ei) = split(/\-/);
	    if ($fi >= scalar(@FOREIGN) || $ei >= scalar(@ENGLISH)) {
		print STDERR "alignment point ($fi,$ei) out of range (0-$#FOREIGN,0-$#ENGLISH) in line $alignment_id, ignoring\n";
	    }
	    else {
		# local counts
		$FOREIGN_ALIGNED{$fi}++;
		$ENGLISH_ALIGNED{$ei}++;
		
		# global counts
		$WORD_TRANSLATION{$FOREIGN[$fi]}{$ENGLISH[$ei]}++;
		$TOTAL_FOREIGN{$FOREIGN[$fi]}++;
		$TOTAL_ENGLISH{$ENGLISH[$ei]}++;
	    }
        }

        # unaligned words
        for(my $ei=0;$ei<scalar(@ENGLISH);$ei++) {
          next if defined($ENGLISH_ALIGNED{$ei});
          $WORD_TRANSLATION{"NULL"}{$ENGLISH[$ei]}++;
          $TOTAL_ENGLISH{$ENGLISH[$ei]}++;
          $TOTAL_FOREIGN{"NULL"}++;
        }
        for(my $fi=0;$fi<scalar(@FOREIGN);$fi++) {
          next if defined($FOREIGN_ALIGNED{$fi});
          $WORD_TRANSLATION{$FOREIGN[$fi]}{"NULL"}++;
          $TOTAL_FOREIGN{$FOREIGN[$fi]}++;
          $TOTAL_ENGLISH{"NULL"}++;
        }
    }
    print STDERR "\n";
    close(A);
    close(F);
    close(E);

    open(F2E,">$f2e") or die "ERROR: Can't write $f2e";
    open(E2F,">$e2f") or die "ERROR: Can't write $e2f";

    foreach my $f (keys %WORD_TRANSLATION) {
	foreach my $e (keys %{$WORD_TRANSLATION{$f}}) {
	    printf F2E "%s %s %.7f %d %d\n",$e,$f,$WORD_TRANSLATION{$f}{$e}/$TOTAL_FOREIGN{$f},$WORD_TRANSLATION{$f}{$e},$TOTAL_FOREIGN{$f};
	    printf E2F "%s %s %.7f %d %d\n",$f,$e,$WORD_TRANSLATION{$f}{$e}/$TOTAL_ENGLISH{$e},$WORD_TRANSLATION{$f}{$e},$TOTAL_ENGLISH{$e};
	}
    }
    close(E2F);
    close(F2E);
    print STDERR "Saved: $f2e and $e2f\n";
}

sub open_compressed {
    my ($file) = @_;
    print "FILE: $file\n";

    # add extensions, if necessary
    $file = $file.".bz2" if ! -e $file && -e $file.".bz2";
    $file = $file.".gz"  if ! -e $file && -e $file.".gz";
   
    # pipe zipped, if necessary
    return "$BZCAT $file|" if $file =~ /\.bz2$/;
    return "$ZCAT $file|"  if $file =~ /\.gz$/;    
    return $file;
}

sub fix_spaces() {
        my ($in) = @_;
        $$in =~ s/[ \t]+/ /g; $$in =~ s/[ \t]$//; $$in =~ s/^[ \t]//;    
}

