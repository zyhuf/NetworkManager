SUBDIRS = modem btgprs

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
        -DSYSCONFDIR=\"$(sysconfdir)\"			\
        -DICONDIR=\""$(datadir)/pixmaps"\"              \
        -DGLADEDIR=\""$(gladedir)"\"                    \
        -DG_DISABLE_DEPRECATED                          \
        -DGDK_DISABLE_DEPRECATED                        \
        -DGNOME_DISABLE_DEPRECATED                      \
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
        $(NULL)

CLEANFILES = $(server_DATA) *.bak *.gladep *~

EXTRA_DIST =                            \
        $(glade_DATA)                   \
        $(NULL)



