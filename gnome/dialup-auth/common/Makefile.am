
noinst_LTLIBRARIES = libnmdialupauthcommon.la

libnmdialupauthcommon_la_CFLAGS =		\
        $(DBUS_CFLAGS)                                  \
        $(GTHREAD_CFLAGS)                               \
        $(DBUS_GLIB_CFLAGS)                             \
	$(GNOMEKEYRING_CFLAGS)				\
	$(LIBGNOMEUI_CFLAGS)				\
	$(GTK_CFLAGS)					\
        -DDBUS_API_SUBJECT_TO_CHANGE                    \
        -DG_DISABLE_DEPRECATED                          \
        -DGDK_DISABLE_DEPRECATED                        \
        -DGNOME_DISABLE_DEPRECATED                      \
        -DGNOMELOCALEDIR=\"$(datadir)/locale\"  \
        $(NULL)

libnmdialupauthcommon_la_SOURCES =		\
	gnome-two-password-dialog.h		\
	gnome-two-password-dialog.c

libnmdialupauthcommon_la_LIBADD =		\
	$(GTK_LIBS)				\
	$(LIBGNOMEUI_LIBS)			\
	$(GNOMEKEYRING_LIBS)			\
	$(NULL)

CLEANFILES = *~
