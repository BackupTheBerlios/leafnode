# -*- makefile -*-
#
# (C) Copyright 2002 - 2012 by Matthias Andree <matthias.andree@gmx.de>.
# See COPYING for the license conditions.

AUTOMAKE_OPTIONS	= foreign dist-bzip2 no-dist-gzip no-installinfo 1.11 parallel-tests check-news color-tests

sysconf_DATA	= config.example \
	       filters.example

GENERATED_MANS=lsmac.1

man_MANS = \
	applyfilter.8 \
	checkgroups.8 \
	filterfile.5 \
	fetchnews.8 \
	leafnode-version.1 \
	leafnode.8 \
	moderators.5 \
	newsq.1 \
	rnews.8 \
	sendbatch.1 \
	texpire.8 \
	$(GENERATED_MANS)

lsmac.1: tools/lsmac.pl
	pod2man $(srcdir)/tools/lsmac.pl >$@ || { rm -f $@ ; false ; }

dist_MANS = \
	rbcloselist.3 \
	rbdelete.3 \
	rbdestroy.3 \
	rbfind.3 \
	rbinit.3 \
	rblookup.3 \
	rbopenlist.3 \
	rbreadlist.3 \
	rbsearch.3 \
	rbwalk.3

noinst_LIBRARIES	= liblnutil.a
liblnutil_a_SOURCES	= \
	activutil.c \
	activutil.h \
	activutil_resolve.c \
	artutil.c \
	attributes.h \
	bsearch_range.h \
	bsearch_range.c \
	cmp_firstcolumn.c \
	configparam.c \
	configparam.h \
	configutil.c \
	critmem.h \
	critmem_calloc.c \
	critmem_malloc.c \
	critmem_realloc.c \
	critmem_strdup.c \
	debugutil.c \
	dirutil.c \
	expire_lookup.c \
	filterutil.c \
	fopen_reg.c \
	format.h \
	format_ulong.c \
	format_ulong0.c \
	get.h \
	get_any.c \
	getaline.c \
	getfoldedline.c \
	getline.c \
	getline.h \
	getwatermarks.c \
	gmtoff.c \
	grouplist.c \
	grouplist.h \
	groupselect.c \
	groupselect.h \
	h_error.c \
	h_error.h \
	interesting.c \
	lclint_fixes.h \
	leafnode.h \
	link_force.c \
	link_force.h \
	ln_dir.h \
	ln_log.c \
	ln_log.h \
	ln_log_open.c \
	localutil.c \
	lockfile.c \
	log_chmod.c \
	log_close.c \
	log_fchmod.c \
	log_fclose.c \
	log_fsync.c \
	log_rename.c \
	log_unlink.c \
	log_moveto.c \
	mailto.c \
	mailto.h \
	masock.h \
	masock_sa2addr.c \
	masock_sa2name.c \
	masock_sa2port.c \
	mastrcpy.c \
	mastring.c \
	mastring.h \
	mastrncpy.c \
	midutil.c \
	miscutil.c \
	mkdir_parent.c \
	moderated.c \
	msgid.h \
	msgid_hash.c \
	msgid_sanitize.c \
	nfswrite.c \
	nntputil.c \
	parserange.c \
	pcrewrap.c \
	pcrewrap.h \
	putaline.c \
	queues.c \
	readheaders.c \
	redblack.c \
	redblack.h \
	sgetcwd.h \
	sgetcwd.c \
	sort.c \
	store.c \
	strutil.c \
	sync_dir.c \
	sync_link.c \
	system.h \
	tab2spc.c \
	timeout_getaline.c \
	touch.c \
	ugid.h \
	ugid_gensure.c \
	ugid_gget.c \
	ugid_ggetbyuname.c \
	ugid_gset.c \
	ugid_uensure.c \
	ugid_uget.c \
	ugid_ugetbyuname.c \
	ugid_uset.c \
	validatefqdn.c \
	validatefqdn.h \
	version.h \
	wildmat.c \
	writes.c \
	xoverutil.c
nodist_liblnutil_a_SOURCES= \
	configparam_data.c \
	config.c \
	config_defs.h

# PROGRAMS lists the programs to build
sbin_PROGRAMS = \
	applyfilter \
	checkgroups \
	fetchnews \
	leafnode \
	rnews \
	texpire

sbin_SCRIPTS =		tools/sendbatch.bash
dist_bin_SCRIPTS =	tools/lsmac.pl
dist_sysconf_DATA=	tools/uucp

bin_PROGRAMS = \
	leafnode-version \
	newsq

noinst_PROGRAMS = \
	amiroot \
	b_sortnl \
	lsort

check_PROGRAMS= \
		t.getwatermarks \
		xsnprintf strutil \
		grouplist \
		t.mgetheader

strutil_CPPFLAGS=$(AM_CPPFLAGS) -DTEST
grouplist_CPPFLAGS=$(AM_CPPFLAGS) -DTEST

TESTS= \
	xsnprintf t.mgetheader

EXTRA_DIST = \
	$(sysconf_DATA) \
	aclocal.m4 \
	arc4random.3 \
	arc4random.c \
	arc4random.h \
	CHANGES-FROM-LEAFNODE-1 \
	config.table \
	config.h.in \
	COPYING.LGPL \
	CREDITS \
	DEBUGGING \
	Doxyfile \
	ENVIRONMENT \
	FAQ.tex \
	FAQ.html \
	FAQ.pdf \
	README-FQDN.tex \
	README-FQDN.html \
	README-FQDN.pdf \
	genconfigdefs.awk genconfigparam_data.awk \
	leafnode.spec \
	leafnode.spec.in \
	mergesort.3 \
	README \
	README-leaf.node \
	README.html \
	strlcpy.3 strlcpy.h strlcpy.c \
	update.sh \
	tools/archivefaq.pl \
	tools/make_pass.pl \
	tools/README \
	tools/sendbatch.bash.in \
	tools/simple-batcher.sh \
	$(GENERATED_MANS)

BUILT_SOURCES = \
	config.c \
	configparam_data.c \
	config_defs.h \
	config_paths.h

config_paths.h: Makefile
	echo >$@  '#define GZIP  ((const char *)"$(GZIP)")' && \
	echo >>$@ '#define BZIP2 ((const char *)"$(BZIP2)")' \
	|| { rm -f $@ ; false ; }

FAQ.css FAQ.html: FAQ.tex
	( tmp="`mktemp -d $${TMPDIR-/tmp}/leafnode-FAQ.XXXXXXXXXX`" && cp $(srcdir)/FAQ.tex $$tmp && cd $$tmp && \
	hevea FAQ.tex && hevea FAQ.tex && cd - && cp $$tmp/FAQ.html . && rm -rf $$tmp)

README-FQDN.css README-FQDN.html: README-FQDN.tex
	( tmp="`mktemp -d $${TMPDIR-/tmp}/leafnode-README-FQDN.XXXXXXXXXX`" && cp $(srcdir)/README-FQDN.tex $$tmp && cd $$tmp && \
	hevea README-FQDN.tex && hevea README-FQDN.tex && cd - && cp $$tmp/README-FQDN.html . && rm -rf $$tmp)

FAQ.pdf: FAQ.tex
	pdflatex $(srcdir)/FAQ.tex && pdflatex $(srcdir)/FAQ.tex || { rm -f FAQ.pdf ; exit 1 ; }

README-FQDN.pdf: README-FQDN.tex
	pdflatex $(srcdir)/README-FQDN.tex && pdflatex $(srcdir)/README-FQDN.tex || { rm -f README-FQDN.pdf ; exit 1 ; }

# special rule to prevent recursion on liblnutil.a
amiroot_SOURCES		= amiroot.c
amiroot_LDADD 		=

b_sortnl_SOURCES	= b_sortnl.c critmem_malloc.c critmem_realloc.c
b_sortnl_LDADD		= @LIBOBJS@

# programs
LDADD			= @LIBOBJS@ liblnutil.a
applyfilter_SOURCES	= applyfilter.c
fetchnews_SOURCES	= fetchnews.c fetchnews_check_date.c fetchnews.h
leafnode_SOURCES	= nntpd.c
lsort_SOURCES		= lsort.c
rnews_SOURCES		= rnews.c
texpire_SOURCES		= texpire.c
checkgroups_SOURCES	= checkgroups.c
newsq_SOURCES		= newsq.c
leafnode_version_SOURCES= leafnode-version.c
t_getwatermarks_SOURCES	= t.getwatermarks.c
t_mgetheader_SOURCES=	  t.mgetheader.c

CLEANFILES = FAQ.aux FAQ.log FAQ.toc \
	     README-FQDN.aux README-FQDN.log README-FQDN.toc

DISTCLEANFILES = $(BUILT_SOURCES) README $(GENERATED_MANS) \
	config.table.sorted

# GENERATED SOURCES ========================================
config.c: Makefile
	@echo "creating config.c"
	@echo  >.$@ "/* This file is automatically generated by Makefile"
	@echo >>.$@ " * *** DO NOT EDIT ***"
	@echo >>.$@ " */"
	@echo >>.$@ "#include \"leafnode.h\""
	@echo >>.$@ "const char *bindir = \"$(bindir)\";"
	@echo >>.$@ "const char *def_spooldir = \"$(SPOOLDIR)\";"
	@echo >>.$@ "const char *sysconfdir = \"$(sysconfdir)\";"
	@echo >>.$@ "const char *version = \"@VERSION@\";"
	@echo >>.$@ "const char *DEFAULTMTA = \"$(DEFAULTMTA)\";"
	@echo >>.$@ "const char *RUNAS_USER = \"$(RUNAS_USER)\";"
	if cmp -s .$@ $@ ; then touch $@ ; rm -f .$@ ; else mv .$@ $@ ; fi

config_defs.h: $(srcdir)/config.table $(srcdir)/configparam.h genconfigdefs.awk
	$(AWK) -F, -f $(srcdir)/genconfigdefs.awk \
	<$(srcdir)/config.table >$@ || { rm -f $@ ; false; }

configparam_data.c: $(srcdir)/config.table b_sortnl$(EXEEXT) genconfigparam_data.awk
	rm -f config.table.sorted
	./b_sortnl$(EXEEXT) <$(srcdir)/config.table >config.table.sorted
	$(AWK) -f $(srcdir)/genconfigparam_data.awk <config.table.sorted >$@ \
	|| { rm -f $@ ; false ; }

.PHONY:	update
update: lsort
	@echo
	@echo "### Note that make update is only needed for updates from very old"
	@echo "### leafnode versions, namely, < 1.6 (around 1998)."
	@echo
	$(SHELL) "$(top_srcdir)/update.sh" "$(SPOOLDIR)" "$(sysconfdir)" "$(SPOOLDIR)/leaf.node/lock.file" "$(RUNAS_USER)" "$(RUNAS_GROUP)"

install-data-hook:
	$(INSTALL) -d -m 02770 $(DESTDIR)$(SPOOLDIR)
	$(INSTALL) -d -m 02770 $(DESTDIR)$(SPOOLDIR)/leaf.node
	-./amiroot && chown $(RUNAS_USER) $(DESTDIR)$(SPOOLDIR)
	-./amiroot && chown $(RUNAS_USER) $(DESTDIR)$(SPOOLDIR)/leaf.node
	-./amiroot && chgrp $(RUNAS_GROUP) $(DESTDIR)$(SPOOLDIR)
	-./amiroot && chgrp $(RUNAS_GROUP) $(DESTDIR)$(SPOOLDIR)/leaf.node
	rm -f $(DESTDIR)$(sbindir)/leafnode-version # now in $(bindir)
	./amiroot && p=$(DESTDIR)$(bindir)/newsq && chown 0 $$p \
		&& chgrp $(RUNAS_GROUP) $$p && chmod 2111 $$p  ; true
	@echo
	@echo "### If upgrading from leafnode before 1.6,"
	@echo "### run:  make update"
	@echo
	@echo "### If upgrading from leafnode-1 or a snapshot older than 2002,"
	@echo "### you must fix the message.id index, run: $(sbindir)/texpire -r"
	@echo
	@echo "### If upgrading from a version before 20040326a"
	@echo "### for the first time, run: chmod -R o= $(SPOOLDIR)"
	@echo "### Note this command may take a long time to run."
	@echo
	@echo "Then edit /etc/inetd.conf to start $(sbindir)/leafnode"
	@echo "and restart inetd. Also read the instructions on updating in the README file."
	@echo

uninstall-local:
	-rm -f $(sysconfdir)/config.example
	-rmdir $(sysconfdir)
	-rm -r $(SPOOLDIR)/leaf.node
	-rmdir $(SPOOLDIR)/in.coming
	-rmdir $(SPOOLDIR)/out.going
	-rmdir $(SPOOLDIR)/failed.postings
	-rmdir $(SPOOLDIR)/backup.moderated
	-rmdir $(SPOOLDIR)/temp.files
	-rmdir $(SPOOLDIR)/interesting.groups
	-rmdir $(SPOOLDIR)/message.id
	@echo
	@echo "Edit /etc/inetd.conf to remove $(sbindir)/leafnode and restart inetd."

.PHONY:	doxygen
doxygen:
	-rm -rf $(srcdir)/dox/html/
	cd $(srcdir) && doxygen

README:	README.html $(LYNX)
	if test "$(LYNX)" != "no" ; then \
		rm -f README ; \
		$(LYNX) -dump $(srcdir)/README.html >$@ ; \
	fi

.PHONY:	rpm
rpm: dist-bzip2 leafnode.spec
	-ln -s -f `pwd`/leafnode-$(VERSION).tar.bz2 $(RPMSRC)/SOURCES
	-ln -s -f `pwd`/leafnode.spec $(RPMSRC)/SPECS
	unset IFS ; case `uname -m` in i?86) targ=--target=i586 ;; *) targ= ;; esac ; \
	env LANG=C LC_MESSAGES=C rpmbuild -ba $$targ $(RPMSRC)/SPECS/leafnode.spec 2>&1 \
	| grep '^Wrote:' | $(SED) -e 's/^Wrote: //' \
	| while read r ; do mv -f "$$r" . || exit 1 ; done
# this target expects a .rsyncs file with lines of this format:
# host:directory/
# it will call rsync from its source directory to the destination for
# each of them, running them in parallel
rsync:	distdir $(srcdir)/.rsyncs
	@( cat $(srcdir)/.rsyncs | sed -e 's}^}rsync -av --delete $(PACKAGE)-$(VERSION)/ }; s/\($$\)/ \&/;' ; echo "wait" ) | $(SHELL) -x
