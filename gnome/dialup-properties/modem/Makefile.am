INCLUDES = -I${top_srcdir}/gnome/dialup-properties

lib_LTLIBRARIES = libnm-modem-properties.la

libnm_modem_properties_la_SOURCES = 			\
				nm-modem-properties.c

gladedir = $(datadir)/gnome-dialup-properties
glade_DATA = nm-modem-dialog.glade

libnm_modem_properties_la_CFLAGS =                      \
        $(GLADE_CFLAGS)                                 \
        $(GTK_CFLAGS)                                   \
        $(GCONF_CFLAGS)                                 \
        $(LIBGNOMEUI_CFLAGS)                            \
	$(DBUS_CFLAGS)					\
        -DICONDIR=\""$(datadir)/icons"\"                \
        -DGLADEDIR=\""$(gladedir)"\"                    \
	-DDBUS_API_SUBJECT_TO_CHANGE			\
        -DG_DISABLE_DEPRECATED                          \
        -DGDK_DISABLE_DEPRECATED                        \
        -DGNOME_DISABLE_DEPRECATED                      \
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"		\
        -DVERSION=\"$(VERSION)\"

libnm_modem_properties_la_LIBADD =       \
        $(GLADE_LIBS)                   \
        $(GTK_LIBS)                     \
        $(GCONF_LIBS)                   \
        $(LIBGNOMEUI_LIBS)

CLEANFILES = *.bak *.gladep *~

EXTRA_DIST =                            \
        $(glade_DATA)
