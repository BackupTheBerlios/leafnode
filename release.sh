#! /bin/sh

set -e
if test "x`cvs update | grep -v ^? | grep -v release\\.sh`" != "x" ; then
    echo "commit your changes to CVS first!"
    exit 1
fi
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
cp -p NEWS TODO UPDATING $dest/
if test ! -e leafnode-$vers.tar.bz2.asc -o ! leafnode-$vers.tar.bz2.asc -nt leafnode-$vers.tar.bz2 ; then
    ( cd $dest ; gpg -ba --sign leafnode-$vers.tar.bz2 )
fi
vim $dest/RELEASE
vim $dest/HEADER.html
fi
perl addpatches.pl
synchome.sh &
ssh krusty cvsup -L2 supfile &
wait

[ -f leafnode-ann.$vers ] && exit 0
tmp=`mktemp leafnode-ann.$vers.XXXXXXXXXX`
trap "rm -f $tmp" 0
rm -f $tmp
cat <<_EOF >$tmp
Leafnode $vers is available from
http://home.pages.de/~mandree/leafnode/beta/

NEWS:
_EOF

sed '/^$/q;s/^- /+ /' NEWS >>$tmp
gpg --clearsign $tmp

nail -v -s "leafnode-$vers snapshot available" \
    leafnode-list@dt.e-technik.uni-dortmund.de <$tmp.asc \
&& touch leafnode-ann.$vers
rm -f $tmp
