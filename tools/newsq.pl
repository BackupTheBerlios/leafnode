#!/usr/bin/perl

$spooldir = "/var/spool/news";

undef $/;
if ( chdir "$spooldir/out.going" && opendir( DIR, "." ) ) {
    @files = readdir( DIR );
    closedir( DIR );

    foreach (@files) {
	if ( open(F, "< $_") ) {
	    undef $subject, $newsgroups, $from;
	    $_ = <F>;
	    close F;
	    s/\n\n.*//s;
	    s/\r//gs;
	    s/\n\s+/ /sg;
	    foreach ( split( /\n/, $_ ) ) {
		$subject = $1 if ( /^Subject:\s+(.*)/i );
		$newsgroups = $1 if ( /^Newsgroups:\s+(.*)/i );
		$from = $1 if ( /^From:\s+(.*)/i );
	    }
	    print $from, " in ", $newsgroups, "\n\t", $subject, "\n", 
		if ( $subject ne "" && $from ne "" && $newsgroups ne "" );
	}
    }
}
