#! /bin/sh
git log --decorate --stat --name-status --abbrev-commit --date=short \
  --pretty=format:"=== %ad  %an <%ae>%n%s%n%b%n" -C -C \
| perl -ne '
    chomp;
    $l = $_;
    if (/^===/) {
	if ($last ne $l) {
	    print substr($l, 4), "\n";
	}
	$last = $l;
	$chg = 1;
	$empty = 0;
    } else {
	if ($l) {
	    printf "%s%s\n", ($chg ? "\n  * " : "    "), $l;
	    $empty = 0;
	} else {
	    print "\n" unless $empty;
	    $empty = 1;
	}
	$chg = 0;
    }' \
| uniq
