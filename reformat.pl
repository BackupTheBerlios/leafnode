#! /usr/bin/perl -w
# (C) 2001 by Matthias Andree

# GPL 2

# reformats C code, requires GNU indent 2.2.5

use strict;
$ENV{'PATH'} = '/usr/local/bin:/bin:/usr/bin';

# ----------------------------------------------------------------------

my @indent = qw/indent -bad -bap -bbo -nbc -br -brs -c33 -cd33 -ncdb -ce
-ci4 -cli0 -cp33 -cs -d0 -di1 -nfc1 -nfca -hnl -i4 -ip0 -l78 -lp -npcs
-nprs -nsc -sob -nss/;

# ----------------------------------------------------------------------

sub safe_pipe_fork($) {
    my ($pid, $sleep_count);
    my $name=shift;

    do {
        $pid = open(KID, $name);
        unless (defined $pid) {
            warn "cannot fork: $!";
            die "bailing out" if $sleep_count++ > 6;
            sleep 10;
        }
    } until defined $pid;

    return ($pid);
}

sub safe_pipe_read($@) {
    my $fail_ok = shift;
    my @cmd = @_;
    my $pid = safe_pipe_fork('-|');

    if ($pid) { # parent
        my @a=<KID>; 
        my $ret = close KID;
	if(not $ret and not $fail_ok) { die "child exited: $?"; }
        return @a;
    } else {
        exec { $cmd[0] } @cmd;
        exit -1;
    }
}

sub process($ )
{
    my $f = shift;

    my @a = safe_pipe_read(0, @indent, '-o','/dev/stdout',$f);
    @a = map {
	chomp;
	$_;
    } @a;
    open F, ">$f.new" or die "cannot open $f.new: $!";
    print F join("\n", @a), "\n" or die "write error on $f.new: $!";
    close F or die "cannot close $f.new: $!";
    my $rc = system {'cmp'} ('cmp', $f, "$f.new");
    if ($rc >= 256) {
	print STDERR "$f: formatting changed, replacing\n";
	if (-f "$f.old") {
	    unlink "$f.old" or die "cannot unlink $f.old: $!";
	}
	rename $f, "$f.old" or die "cannot rename $f -> $f.old: $!";
	rename "$f.new", $f or die "cannot rename $f.new -> $f: $!";
    } else {
	print STDERR "$f: formatting unchanged or cmp died\n";
	unlink "$f.new" or die "cannot unlink $f.new: $!";
    }
}

# ----------------------------------------------------------------------

my @a = safe_pipe_read(1, 'indent', '--version');
die "requires GNU indent 2.2.5" if (!@a or $a[0] !~ m/GNU indent 2\.2\./);

my $f;
while ($f = shift @ARGV) {
    if (-w $f) { process ($f); }
}

exit 0;

