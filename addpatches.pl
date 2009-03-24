#! /usr/bin/perl

use strict;

# prototypes
sub gendiff($$\@);

my $pat = 'leafnode-2.0.0.alpha';
my $bookmark = 'SCRIPT MARKER #1 ';
my $tar = '/bin/tar';
my $diff = '/usr/bin/diff';
my $gzip = '/usr/bin/gzip';

chdir "$ENV{HOME}/public_html/leafnode/beta" or die "chdir: $!";
my @tarballs = sort <$pat*.tar.bz2>;

open F, "HEADER.html" or die "open: $!";
my $lastver;
my $havemarker;
while(<F>) {
    if (/$bookmark/) {
	$havemarker++;
    }
    next unless $havemarker;
    if (/upgrade-[^-]+-to-([^-]+).patch.gz/) {
	$lastver = $1;
	last;
    }
}
close F;

print "last version: $lastver\n";
my $lastver_save = $lastver;
@tarballs = grep { $_ gt "$lastver" }
		map { s/\Q$pat\E(.*)\.tar\.bz2/$1/; $_; } @tarballs;

print "newer versions: ", join(" ", @tarballs), "\n";
my $mostcurrent = $tarballs[$#tarballs];
if (!@tarballs) { $mostcurrent = $lastver; }

my @addtxt;
foreach my $curver (@tarballs) {
    exit 1 unless defined gendiff($lastver, $curver, @addtxt);
    $lastver = $curver;
}

print "inserting:\n", join("", @addtxt);

open F, "HEADER.html" or die "open: $!";
my $state = 0;
open O, ">HEADER.html.new" or die "open: $!";
while (<F>) {
    if ($state == 0) {
	s/\Q$lastver_save\E/$mostcurrent/g;
    }
    print O $_;
    if (/$bookmark/) {
	print O join("", @addtxt);
	$state++;
    }
}
close O or die "cannot close: $!";
rename "HEADER.html.new", "HEADER.html" or die "rename: $!";
close F;

exit;

sub gendiff($$\@) {
    my ($last, $cur, $ary) = @_;

    system ($tar, "-xjf", "$pat$last.tar.bz2") and return 1;
    system ($tar, "-xjf", "$pat$cur.tar.bz2") and return;
    system ("$diff -u $pat$last $pat$cur | $gzip -9cf >upgrade-$last-to-$cur.patch.gz");
    system ("rm -rf $pat$last $pat$cur");
    system ("gpg -ba --sign upgrade-$last-to-$cur.patch.gz");
    unshift @$ary, "    <li><a href=\"upgrade-$last-to-$cur.patch.gz\">$last to\n"
    . "        $cur</a> <a\n"
    . "        href=\"upgrade-$last-to-$cur.patch.gz.asc\">[signature]</a></li>\n";
    return 1;
}
