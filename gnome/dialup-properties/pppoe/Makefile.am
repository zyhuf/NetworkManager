INCLUDES = -I${top_srcdir}/gnome/dialup-properties -I${top_srcdir}/include

lib_LTLIBRARIES = libnm-pppoe-properties.la

libnm_pppoe_properties_la_SOURCES = 			\
				nm-pppoe-properties.c

gladedir = $(datadir)/gnome-dialup-properties
glade_DATA = nm-pppoe-dialog.glade

libnm_pppoe_properties_la_CFLAGS =                      \
        $(GLADE_CFLAGS)                                 \
        $(GTK_CFLAGS)                                   \
        $(GCONF_CFLAGS)                                 \
        $(LIBGNOMEUI_CFLAGS)                            \
	$(DBUS_CFLAGS)					\
        -DICONDIR=\""$(datadir)/icons"\"                \
        -DGLADEDIR=\""$(gladedir)"\"                    \
        -DG_DISABLE_DEPRECATED                          \
        -DGDK_DISABLE_DEPRECATED                        \
        -DGNOME_DISABLE_DEPRECATED                      \
	-DDBUS_API_SUBJECT_TO_CHANGE			\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"		\
        -DVERSION=\"$(VERSION)\"

libnm_pppoe_properties_la_LIBADD =      \
        $(GLADE_LIBS)                   \
        $(GTK_LIBS)                     \
        $(GCONF_LIBS)                   \
	$(DBUS_LIBS)			\
        $(LIBGNOMEUI_LIBS)

CLEANFILES = *.bak *.gladep *~

EXTRA_DIST =                            \
        $(glade_DATA)

