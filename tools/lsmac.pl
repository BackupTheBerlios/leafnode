#! /usr/bin/perl

=head1 NAME

lsmac.pl - List directories with file mtimes, atimes and ctimes.

=head1 SYNOPSIS

 lsmac.pl [options] [{file|directory} [...]]
 lsmac.pl --help   or   lsmac.pl -h
 lsmac.pl --man

=head1 OPTIONS

 -h, --help      print this short help
 -l, --local     print times in local time zone rather than GMT
 -d, --directory print directories' times rather than descending into them
 --man           print the manual page

=head1 DESCRIPTION

For each directory given on the command line, lsmac.pl will list the
directory contents with mtime, atime and ctime for each of the entries. The columns are, in this order:
ctime, mtime, atime, number of links, size and file name.

If no directory is given, lsmac.pl will process the current directory.

=head1 AUTHOR

=over Matthias Andree

=item * Matthias Andree <matthias.andree@gmx.de>

=head1 LICENSE

lsmac.pl C<$Revision: 1.5 $>, Copyright (C) 2001,2003 Matthias Andree.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

=back

=cut

use strict;
use File::stat; # for symbolic stat names
use HTTP::Date; # for time2iso
use Getopt::Long;

$ENV{PATH}="/usr/local/bin:/bin:/usr/bin";

eval 'use Pod::Usage;';
if ($@) {
    eval 'sub pod2usage {
	print STDERR "Usage information would be presented here if you had Pod::Usage installed.\n"
		     . "Try: perl -MCPAN -e \'install Pod::Usage\'\nAbort.\n";
	exit 2;
    }';
}

$0 =~ m|(.*/)?([^/]+)|;
my $myname = $2;
$myname =~ tr/0-9a-zA-Z.-_//cd;

my %opt = ();
GetOptions(\%opt, 'help|h|?', 'local|l', 'directory|d', 'man')
    or pod2usage(-verbose => 0);
pod2usage(-verbose => 1) if $opt{help};
pod2usage(-verbose => 2) if $opt{man};

my $cvt;
if (not $opt{local}) {
    #default: UTC
    $cvt = sub{ return HTTP::Date::time2isoz($_[0]); };
} else {
    $cvt = sub{ return HTTP::Date::time2iso($_[0]); };
}

unshift (@ARGV, '.') if @ARGV == 0;

sub lsf($ ) {
    my $name = shift;
    my $st;


    if(($st = lstat($name))) {
	print 
	&$cvt($st->ctime), " ",
	&$cvt($st->mtime), " ",
	&$cvt($st->atime), " ",
	sprintf("%3d %8d", $st->nlink, $st->size), " ", $name, "\n";
    } else {
	warn "$name: $!";
    }
}

sub ls($ ) {
    my $f;
    my $dir = shift;

    if (-d $dir and not $opt{directory}) {
	opendir (D, $dir) or do {
	    warn "cannot read $dir: $!";
	    return;
	};

	while ($f = readdir(D)) {
	    lsf($dir . "/" . $f);
	}

	closedir D or warn "$dir: $!";
    } else {
	lsf($dir);
    }
}

while(my $d = shift @ARGV) {
    ls ($d);
}
