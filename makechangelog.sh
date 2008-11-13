#! /bin/sh
git log --no-merges \
| git name-rev --tags --stdin
