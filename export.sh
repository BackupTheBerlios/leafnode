#! /bin/sh

a=0
echo "===>  Pushing to own home directory"
git push "$HOME"/public_html/leafnode/leafnode-2.git/ || a=1
echo "===>  Pushing to BerliOS"
git push ssh://m-a@git.berlios.de/gitroot/leafnode/ || a=1
echo "===>  Finished."
if [ $a -eq 0 ] ; then
  echo "Success."
else
  echo "Error, see above for details!"
fi
exit $a
