#! /bin/sh

set -e
repo="$HOME"/public_html/leafnode/beta/darcs/
rsync -a ./ "$repo" \
    --exclude 'build*' \
    --exclude '.*.swp' \
    --exclude '*~' \
    --exclude dox \
    --exclude 'autom4te.cache' \
    --delete --delete-excluded
cd $repo
darcs whatsnew --boring -ls \
| awk '/^a / { printf "%s\0", $2; }' \
| xargs -0 rm -f
