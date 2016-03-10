include $(top_srcdir)/Make_global.am

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src include examples tests
DIST_SUBDIRS = src include examples tests

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libnftnl.pc

## Target to run when building a release
release: dist
	@for file in $(DIST_ARCHIVES); do	\
		md5sum $$file > ../$$file.md5;	\
	done
	@mv $(DIST_ARCHIVES) ../
	@echo
	@echo "Resulting release files:"
	@echo "================================================================="
	@for file in $(DIST_ARCHIVES); do					\
		printf "$$file    \tDistribution tarball\n";			\
		printf "$$file.md5\t"; cat ../$$file.md5 | cut -f1 -d' ';	\
	done
