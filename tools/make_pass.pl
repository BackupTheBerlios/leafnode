#!/usr/bin/perl -wT

use strict;

if (scalar @ARGV != 2) {
    die( "Usage: $0 username password" );
}

my $alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./";

srand();
my $salt = substr($alphabet,int(rand 64),1) . substr($alphabet,int(rand 64),1);
printf("%s:%s\n", $ARGV[0], crypt($ARGV[1], $salt));
exit 0;
