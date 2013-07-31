AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_srcdir}/include \
	-I${top_srcdir}/src \
	-I${top_srcdir}/src/logging \
	-I${top_srcdir}/libnm-util \
	-I${srcdir}/.. \
	$(GLIB_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(LIBNL_CFLAGS)

includes = $(shell find $(top_builddir)/{src,libgsystem} -type d -not -path '*/tests' -not -path '*/tests/*')
AM_CPPFLAGS += $(foreach d, $(includes),-I$d)

AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(GLIB_LIBS) $(GUDEV_LIBS) $(LIBNL_LIBS) $(CODE_COVERAGE_LDFLAGS)

@GNOME_CODE_COVERAGE_RULES@

noinst_PROGRAMS = \
	test-device

test_device_SOURCES = test_device.c
test_device_LDADD = \
	$(top_builddir)/src/libNetworkManager.la \
	$(top_builddir)/libndp/libndp/.libs/libndp.a

@VALGRIND_RULES@
USERTESTS = test-device
ROOTTESTS =

# If explicitly enabled, we can run the root tests
if RUN_ROOT_TESTS
TESTS = $(USERTESTS) $(ROOTTESTS)
else
TESTS = $(USERTESTS)
endif
