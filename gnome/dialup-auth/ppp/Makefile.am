INCLUDES = -I${top_srcdir} -I${top_srcdir}/utils -I${top_srcdir}/gnome/dialup-auth/common

libexec_PROGRAMS = nm-ppp-auth-dialog

nm_ppp_auth_dialog_CPPFLAGS =			\
	$(GTHREAD_CFLAGS)			\
	$(GTK_CFLAGS)				\
	$(LIBGNOMEUI_CFLAGS)			\
	$(GNOMEKEYRING_CFLAGS)			\
	-DICONDIR=\""$(datadir)/icons"\"	\
	-DGLADEDIR=\""$(gladedir)"\"		\
	-DBINDIR=\""$(bindir)"\"		\
	-DDBUS_API_SUBJECT_TO_CHANGE		\
	-DG_DISABLE_DEPRECATED			\
	-DGDK_DISABLE_DEPRECATED		\
	-DGNOME_DISABLE_DEPRECATED		\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"	\
	-DVERSION=\"$(VERSION)\"		\
	$(NULL)

nm_ppp_auth_dialog_SOURCES =			\
	main.c					\
	$(NULL)

nm_ppp_auth_dialog_LDADD =			\
	$(GTK_LIBS)				\
	$(LIBGNOMEUI_LIBS)			\
	$(GNOMEKEYRING_LIBS)			\
	$(top_builddir)/gnome/dialup-auth/common/libnmdialupauthcommon.la	\
	$(NULL)

CLEANFILES = *~
