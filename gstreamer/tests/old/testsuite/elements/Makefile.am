include ../Rules

tests_pass = gst-inspect-check struct_size
tests_fail =
tests_ignore =

noinst_HEADERS = struct_i386.h

gst_inspect_check_SOURCES =
gst-inspect-check$(EXEEXT): $(srcdir)/gst-inspect-check.in
	sed s/@[G]ST_MAJORMINOR@/@GST_MAJORMINOR@/ \
		$(srcdir)/gst-inspect-check.in >gst-inspect-check$(EXEEXT)
	chmod +x gst-inspect-check$(EXEEXT)

EXTRA_DIST = gst-inspect-check.in
