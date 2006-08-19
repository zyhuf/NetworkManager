INCLUDES = -I${top_srcdir}                       \
           -I${top_srcdir}/include               \
           -I${top_srcdir}/utils                 \
           -I${top_srcdir}/src			\
           -I${top_srcdir}/src/named-manager


noinst_LTLIBRARIES = libdialup-manager.la

libdialup_manager_la_SOURCES = \
	nm-dialup-manager.h \
	nm-dialup-manager.c \
	nm-dialup-connection.h \
	nm-dialup-connection.c \
	nm-dbus-dialup.h \
	nm-dbus-dialup.c \
	nm-dialup-service.h \
	nm-dialup-service.c \
	nm-dialup-act-request.h \
	nm-dialup-act-request.c

libdialup_manager_la_LIBADD = $(DBUS_LIBS) $(GTHREAD_LIBS)

AM_CPPFLAGS =							\
				$(DBUS_CFLAGS)						\
				$(GTHREAD_CFLAGS)					\
				$(HAL_CFLAGS)					\
				-Wall							\
				-DDBUS_API_SUBJECT_TO_CHANGE				\
				-DG_DISABLE_DEPRECATED					\
				-DBINDIR=\"$(bindir)\"					\
				-DPREFIX=\""$(prefix)"\"				\
				-DSYSCONFDIR=\""$(sysconfdir)"\"			\
				-DVERSION="\"$(VERSION)\""				\
				-DLIBDIR=\""$(libdir)"\"				\
				-DLIBEXECDIR=\""$(libexecdir)"\"			\
				-DLOCALSTATEDIR=\""$(localstatedir)"\"		 	\
				-DDATADIR=\"$(datadir)\"				\
				-fPIC

CLEANFILES = *~

