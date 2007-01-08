#! /bin/sh

set -e
if test "x`darcs whatsnew -s | grep -v '^No changes!'`" != "x" ; then
    echo "record your changes in darcs first!"
    exit 1
fi

a=
while [ "$a" != y -a "$a" != n ]
do
    printf "Did you update ChangeLog? [y/n] "
    read a
done
if [ "$a" != y ] ; then echo "Then use makechangelog.pl to generate new entries." ; exit 1 ; fi

builddir=`pwd`/build
dest=~/public_html/leafnode/beta/
vers=`perl -n -l -e 'if (/AM_INIT_AUTOMAKE\(.*,\[?([^]]*)\]?\)/) { print "$1\n"; last; }' configure.ac`
if [ x$QUICK != xquick ] ;then
  UPLOAD=yes
  if echo "$vers" | grep -E "devel|rc" ; then UPLOAD=no ; fi
  echo "test if make works"
  (cd $builddir && make -s)
  echo "release for leafnode $vers"
  (cd $builddir && make -s dist)
  cp -p $builddir/leafnode-$vers.tar.bz2 $dest/
  #cp -p leafnode-$vers.lsm $dest/
  cp -p $builddir/README $dest/leafnode-readme.txt
  #cp -p FAQ.{xml,html,txt,pdf} $dest/
  cp -p ChangeLog $dest/ChangeLog.txt
  cp -p TODO $dest/
  cp -p NEWS $dest/NEWS.txt
  if test ! -e leafnode-$vers.tar.bz2.asc -o ! leafnode-$vers.tar.bz2.asc -nt leafnode-$vers.tar.bz2 ; then
    ( cd $dest ; gpg -ba --sign leafnode-$vers.tar.bz2 )
  fi
  ( 
    echo "   MA `date +%Y-%m-%d` leafnode $vers"
    cat $dest/RELEASE
  ) >$dest/RELEASE.new && mv $dest/RELEASE.new $dest/RELEASE
  vim $dest/RELEASE
fi
perl addpatches.pl
vim $dest/HEADER.html
./export.sh

synchome.sh &
wait

[ -f leafnode-ann.$vers ] && exit 0
tmp=`mktemp leafnode-ann.$vers.XXXXXXXXXX`
trap "rm -f $tmp $tmp.asc" 0
rm -f $tmp
cat <<_EOF >$tmp
Leafnode $vers is available from
http://home.pages.de/~mandree/leafnode/beta/

NEWS:
_EOF

sed '/^$/q;s/^- /+ /' NEWS >>$tmp
gpg --clearsign $tmp

nail -s "leafnode-$vers snapshot available" \
    leafnode-list@dt.e-technik.uni-dortmund.de <$tmp.asc
rm $tmp $tmp.asc
trap - 0
