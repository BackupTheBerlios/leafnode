sbin_SCRIPTS=	sendbatch.bash
dist_bin_SCRIPTS=	lsmac.pl
sysconf_DATA=	uucp
man_MANS=	lsmac.1

GENERATED_DOCS=	$(man_MANS)
CLEANFILES=	$(GENERATED_DOCS)

EXTRA_DIST=	archivefaq.pl \
		make_pass.pl \
		sendbatch.bash.in \
		simple-batcher.sh \
		uucp \
		$(GENERATED_DOCS)

lsmac.1: lsmac.pl
	pod2man $(srcdir)/lsmac.pl >$@ || rm -f $@
