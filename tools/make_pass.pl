#!/usr/bin/perl

$argc = @ARGV;
if ( $argc != 2 ) {
    die( "Usage: make_pass username password" );
}

$alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./";

srand();
$salt = substr($alphabet,int(rand 64),1) . substr($alphabet,int(rand 64),1);
printf("%s %s\n", $ARGV[0], crypt($ARGV[1], $salt));
exit 0;
