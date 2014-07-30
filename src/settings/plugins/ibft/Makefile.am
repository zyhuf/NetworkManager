SUBDIRS = . tests

@GNOME_CODE_COVERAGE_RULES@

pkglib_LTLIBRARIES = libnm-settings-plugin-ibft.la

noinst_LTLIBRARIES = libibft-io.la

libibft_io_la_SOURCES = \
	reader.c \
	reader.h \
	errors.c \
	errors.h

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/ \
	-I$(top_srcdir)/src/logging \
	-I$(top_srcdir)/src/platform \
	-I$(top_srcdir)/src/settings \
	-I$(top_srcdir)/src/posix-signals \
	-I$(top_srcdir)/src/config \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/libnm-util \
	-I$(top_builddir)/libnm-util \
	-DNM_VERSION_MAX_ALLOWED=NM_VERSION_NEXT_STABLE \
	$(GLIB_CFLAGS) \
	$(DBUS_CFLAGS) \
	-DG_LOG_DOMAIN=\""NetworkManager-ibft"\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DSBINDIR=\"$(sbindir)\"

libnm_settings_plugin_ibft_la_SOURCES = \
	plugin.c \
	plugin.h \
	nm-ibft-connection.c \
	nm-ibft-connection.h

libnm_settings_plugin_ibft_la_LDFLAGS = -module -avoid-version
libnm_settings_plugin_ibft_la_LIBADD = \
	$(top_builddir)/libnm-util/libnm-util.la \
	libibft-io.la \
	$(GLIB_LIBS)

CLEANFILES = $(BUILT_SOURCES)

