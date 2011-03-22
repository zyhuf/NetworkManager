INCLUDES = -I${top_srcdir} \
           -I${top_srcdir}/include \
           -I${top_builddir}/marshallers \
           -I${top_srcdir}/src/logging \
           -I${top_srcdir}/src/settings/ \
           -I${top_srcdir}/src/ \
           -I${top_srcdir}/src/modem-manager/ \
           -I${top_srcdir}/src/vpn-manager/ \
           -I${top_srcdir}/libnm-util

EXTRA_DIST = \
	nm-compat-device-cdma.xml \
	nm-compat-device-gsm.xml \
	nm-compat-device-serial.xml \
	nm-compat-device.xml \
	nm-compat-manager.xml \
	nm-compat-settings.xml \
	nm-compat-exported-connection.xml \
	nm-compat-active-connection.xml \
	nm-compat-vpn-connection.xml

nm-compat-device-cdma-glue.h: nm-compat-device-cdma.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_device_cdma --mode=glib-server --output=$@ $<

nm-compat-device-gsm-glue.h: nm-compat-device-gsm.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_device_gsm --mode=glib-server --output=$@ $<

nm-compat-device-serial-glue.h: nm-compat-device-serial.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_device_serial --mode=glib-server --output=$@ $<

nm-compat-device-glue.h: nm-compat-device.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_device --mode=glib-server --output=$@ $<

nm-compat-manager-glue.h: nm-compat-manager.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_manager --mode=glib-server --output=$@ $<

nm-compat-settings-glue.h: nm-compat-settings.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_settings --mode=glib-server --output=$@ $<

nm-compat-exported-connection-glue.h: nm-compat-exported-connection.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_exported_connection --mode=glib-server --output=$@ $<

nm-compat-active-connection-glue.h: nm-compat-active-connection.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_active_connection --mode=glib-server --output=$@ $<

nm-compat-vpn-connection-glue.h: nm-compat-vpn-connection.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=nm_compat_vpn_connection --mode=glib-server --output=$@ $<

BUILT_SOURCES = \
	nm-compat-device-cdma-glue.h \
	nm-compat-device-gsm-glue.h \
	nm-compat-device-serial-glue.h \
	nm-compat-device-glue.h \
	nm-compat-manager-glue.h \
	nm-compat-settings-glue.h \
	nm-compat-exported-connection-glue.h \
	nm-compat-active-connection-glue.h \
	nm-compat-vpn-connection-glue.h

noinst_LTLIBRARIES = libcompat.la

libcompat_la_SOURCES = \
	nm-compat-manager.c \
	nm-compat-manager.h \
	nm-compat-device.c \
	nm-compat-device.h \
	nm-compat-active-connection.c \
	nm-compat-active-connection.h \
	nm-compat-vpn-connection.c \
	nm-compat-vpn-connection.h

libcompat_la_CPPFLAGS = \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	-DG_DISABLE_DEPRECATED

libcompat_la_LIBADD = \
	$(top_builddir)/marshallers/libmarshallers.la \
	$(top_builddir)/src/logging/libnm-logging.la \
	$(DBUS_LIBS) \
	$(GLIB_LIBS)

CLEANFILES = $(BUILT_SOURCES)

