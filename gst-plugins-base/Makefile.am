DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXTERNAL
SUBDIRS_EXT = ext
else
SUBDIRS_EXT =
endif

SUBDIRS = 			\
	pkgconfig		\
	gst-libs 		\
	gst sys $(SUBDIRS_EXT) 	\
	tools 			\
	common 			\
	m4

DIST_SUBDIRS = 			\
	pkgconfig		\
	gst-libs		\
	gst sys ext		\
	tools 			\
	common 			\
	m4

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-plugins-base.spec depcomp \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-base.doap autogen.sh $(win32)

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif

WIN32_COPY = \
	$(top_builddir)/gst-libs/gst/*/*-enumtypes.[ch] \
	$(top_builddir)/_stdint.h

win32-update:
	for f in $(WIN32_COPY); do cp -v $$f win32/common; done; \
	for f in win32/common/*-enumtypes.c; do \
	  echo "Indenting $$f"; \
	  gst-indent $$f; gst-indent $$f; \
	done
	cp -v $(top_builddir)/win32/common/config.h-new \
	    $(top_srcdir)/win32/common/config.h

include $(top_srcdir)/common/coverage/lcov.mak

check: check-exports

# cruft: plugins that have been merged or moved or renamed

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
