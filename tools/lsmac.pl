#! /usr/bin/perl

use strict; 
use File::stat; # for symbolic stat names
use HTTP::Date; # for time2iso

sub ls($ ) {
    my $f;
    my $dir = shift;

    opendir (D, $dir) or do {
	warn "cannot read $dir: $!";
	return;
    };
    
    while ($f = readdir(D)) {
	my $st;

	if(($st = lstat($dir . "/" . $f))) {
	    print 
	      HTTP::Date::time2iso($st->ctime), " ",
	      HTTP::Date::time2iso($st->mtime), " ",
	      HTTP::Date::time2iso($st->atime), " ",
		$st->nlink, " ",
		sprintf("%8d", $st->size), " ", $f, "\n";
	} else {
	    warn "$f: $!";
	}
    }

    closedir D or warn "$dir: $!";
}

while(my $d = shift @ARGV) {
    ls ($d);
}
