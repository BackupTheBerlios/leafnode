#! /usr/bin/env perl
# newsgroup2groupinfo.pl
# (C) 2002 by Matthias Andree
#
# This code is redistributable according to the terms of the GNU GENERAL
# PUBLIC LICENSE, v2.
#
# This program takes a "newsgroup" file on standard input and converts
# it to leafnode's groupinfo format, assuming that all newsgroups are
# "post-allowed", and prints the groupinfo format on standard output.
#
# After an idea of Tim König.

use strict;
my $t = time;
while (<>) {
    chomp;
    my @a = split /\t/, $_, 2;
    print "$_[0]\ty\t0\t0\t$t\t$_[1]\n";
}
