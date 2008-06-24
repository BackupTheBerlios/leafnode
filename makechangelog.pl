#! /usr/bin/env perl

use strict;
use HTTP::Date;

my $re = '\S+\s+\S+\s+\d+\s+\d+:\d+:\d+\s+\S+\s+\d+\s+';
my $cmd = "darcs changes -s";
open F, "-|", "$cmd" or die "cannot run $cmd: $!";
my $o = '';
while(<F>) {
    if (/^($re)/) {
	my $t = HTTP::Date::str2time($1);
	my $s = HTTP::Date::time2isoz($t);
	$s =~ s/(....-..-..) ..:..:..Z/$1/;
	s/^$re/$s  /;
	if ($_ eq $o) { $_ = ''; }
	else { $o = $_; }
    }
    print $_;
}
