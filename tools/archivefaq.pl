#!/usr/bin/perl

# trailing slash!
$archive = "/var/spool/news/faq.archive/";

chdir("/var/spool/news/news/answers/") || die "can't chdir to FAQ directory";

opendir(FAQ, ".") || die "can't open FAQ directory";
@faqs = grep(/^\d+$/, readdir(FAQ));
closedir(FAQ);

undef $/;
foreach $artno ( @faqs ) {
    if ( open(I, "< $artno") ) {
	$_ = <I>;
	close(I);
	if ( $_ =~ /\nArchive-name: (\S+)\n/ ) {
	    $name = $archive . $1;
	    $dir = $name;
	    &mkpdir( $name );
	    unlink( $artno ) unless ( link( $artno, $name ) );
	}
    }
}
	




#
# make parent directory of argument
#
sub mkpdir {
    local( $dir ) = @_;

    $dir =~ s-/[^/]+$--;
    unlink $dir if ( -e $dir && ! -d _ );
    unless ( -d _ ) {
	&mkpdir( $dir );
	mkdir $dir, 0775;
	print "made ", $dir, "\n";
    }
}
