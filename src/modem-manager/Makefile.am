INCLUDES = \
	-I${top_srcdir}/src \
	-I${top_srcdir}/include \
	-I${top_srcdir}/libnm-util \
	-I${top_builddir}/marshallers

noinst_LTLIBRARIES = libmodem-manager.la

libmodem_manager_la_SOURCES = \
	nm-modem-device.c \
	nm-modem-device.h \
	nm-modem-manager.h \
	nm-modem-manager.c \
	nm-modem-types.h

libmodem_manager_la_CPPFLAGS = \
	$(DBUS_CFLAGS)

libmodem_manager_la_LIBADD = \
	$(DBUS_LIBS) \
	$(top_builddir)/marshallers/libmarshallers.la

nm-gsm-device-glue.h: $(top_srcdir)/introspection/nm-device-gsm.xml
	dbus-binding-tool --prefix=nm_gsm_device --mode=glib-server --output=$@ $<

BUILT_SOURCES = \
	nm-gsm-device-glue.h

CLEANFILES = $(BUILT_SOURCES)
