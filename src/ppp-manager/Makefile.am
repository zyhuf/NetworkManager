INCLUDES = -I${top_srcdir}                       \
           -I${top_srcdir}/include               \
           -I${top_srcdir}/utils                 \
           -I${top_srcdir}/src                   \
           -I${top_srcdir}/src/named-manager

#noinst_LTLIBRARIES = libppp-manager.la

#libppp_manager_la_SOURCES =	nm-dbus-vpn.c

#libppp_manager_la_CPPFLAGS =	$(DBUS_CFLAGS)					\
#				$(GTHREAD_CFLAGS)				\
#				$(HAL_CFLAGS)					\
#				-g							\
#				-Wall						\
#				-DDBUS_API_SUBJECT_TO_CHANGE		\
#				-DG_DISABLE_DEPRECATED			\
#				-DBINDIR=\"$(bindir)\"			\
#				-DDATADIR=\"$(datadir)\"			\
#				-DSYSCONFDIR=\"$(sysconfdir)\"


#libppp_manager_la_LIBADD = $(DBUS_LIBS) $(GTHREAD_LIBS)


bin_PROGRAMS = nm-ppp-service

AM_CPPFLAGS =							\
				$(DBUS_CFLAGS)						\
				$(GTHREAD_CFLAGS)					\
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

nm_ppp_service_SOURCES =				\
				nm-ppp-service.c	\
				nm-ppp-service.h

nm_ppp_service_LDADD = 						\
				$(DBUS_LIBS)			\
				$(GTHREAD_LIBS)

nm_pppd_plugindir = $(libdir)/pppd/`grep VERSION $(incdir)/pppd/patchlevel.h  | awk -F\" '{ print $$2 }'`
nm_pppd_plugin_PROGRAMS = nm-pppd-plugin.so

nm_pppd_plugin_so_SOURCES =				\
				nm-pppd-plugin.h	\
				nm-pppd-plugin.c	\
				nm-ppp-service.h

nm_pppd_plugin_so_LDFLAGS =	-shared
nm_pppd_plugin_so_LDADD =	$(DBUS_LIBS)		\
				$(GTHREAD_LIBS)

CLEANFILES = *~

