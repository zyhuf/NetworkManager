SUBDIRS = modem btgprs pppoe

INCLUDES = -I${top_srcdir}/include

bin_PROGRAMS = nm-dialup-properties

nm_dialup_propertiesdir=$(includedir)/NetworkManager

nm_dialup_properties_HEADERS = 					\
	nm-dialup-ui-interface.h

nm_dialup_properties_SOURCES = 					\
					nm-dialup-properties.c		\
					clipboard.c			\
					clipboard.h			\
					nm-dialup-ui-interface.h

gladedir = $(datadir)/gnome-dialup-properties
glade_DATA = nm-dialup-properties.glade

nm_dialup_properties_CFLAGS =                              \
        $(GLADE_CFLAGS)                                 \
        $(GTK_CFLAGS)                                   \
        $(GCONF_CFLAGS)                                 \
        $(GMODULE_CFLAGS)                               \
        $(LIBGNOMEUI_CFLAGS)                            \
	$(DBUS_CFLAGS)					\
        -DSYSCONFDIR=\"$(sysconfdir)\"			\
        -DICONDIR=\""$(datadir)/icons"\"              \
        -DGLADEDIR=\""$(gladedir)"\"                    \
        -DG_DISABLE_DEPRECATED                          \
        -DGDK_DISABLE_DEPRECATED                        \
        -DGNOME_DISABLE_DEPRECATED                      \
	-DDBUS_API_SUBJECT_TO_CHANGE			\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"		\
        -DVERSION=\"$(VERSION)\" \
	-DDIALUP_NAME_FILES_DIR=\""$(sysconfdir)/NetworkManager/dialup"\"	\
        $(NULL)

nm_dialup_properties_LDADD =               \
        $(GLADE_LIBS)                   \
        $(GTK_LIBS)                     \
        $(GCONF_LIBS)                   \
        $(GMODULE_LIBS)                 \
        $(LIBGNOMEUI_LIBS)              \
	$(DBUS_LIBS)			\
        $(NULL)

CLEANFILES = $(server_DATA) *.bak *.gladep *~

EXTRA_DIST =                            \
        $(glade_DATA)                   \
        $(NULL)



