AUTOMAKE_OPTIONS = subdir-objects

include $(GLIB_MAKEFILE)
-include $(INTROSPECTION_MAKEFILE)
include vapi/Makefile.vapigen

man_MANS =
CLEANFILES =
DISTCLEANFILES =
EXTRA_DIST =
noinst_LTLIBRARIES =
lib_LTLIBRARIES =
GLIB_GENERATED =
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS =
INTROSPECTION_COMPILER_ARGS =
SUBDIRS = .
pkgconfig_DATA =
gir_DATA =
typelib_DATA =
libnminclude_HEADERS =
sbin_PROGRAMS =
libexec_PROGRAMS =
dbusservice_DATA =
dbusactivation_DATA =
polkit_policy_DATA =
noinst_DATA =
vapi_DATA =
BUILT_SOURCES =
VAPIGEN_VAPIS =

INSTALL_DATA_HOOKS =
UNINSTALL_HOOKS =
CHECK_LOCALS =

pkgconfigdir = $(libdir)/pkgconfig
girdir = $(datadir)/gir-1.0
typelibdir = $(libdir)/girepository-1.0
libnmincludedir = $(includedir)/libnm
libnmglibincludedir = $(includedir)/libnm-glib
libnmglibvpndir = $(includedir)/libnm-glib
dbusservicedir = $(DBUS_SYS_DIR)
dbusactivationdir = $(datadir)/dbus-1/system-services
dispatcherdir=$(sysconfdir)/NetworkManager/dispatcher.d
polkit_policydir = $(datadir)/polkit-1/actions
servicedir = $(datadir)/dbus-1/system-services
examplesdir = $(docdir)/examples
udevrulesdir = $(UDEV_DIR)/rules.d
rundir = $(runstatedir)/NetworkManager
statedir = $(localstatedir)/lib/NetworkManager
libnmutilincludedir=$(includedir)/NetworkManager
vapidir = $(datadir)/vala/vapi

edit = @sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@DISTRO_NETWORK_SERVICE[@]|$(DISTRO_NETWORK_SERVICE)|g'

include man/Makemodule.am
include shared/Makemodule.am
include introspection/Makemodule.am
include libnm-core/Makefile.libnm-core
include libnm-core/Makemodule.am
include libnm/Makemodule.am
include src/Makemodule.am
include callouts/Makemodule.am
#include clients/Makemodule.am
include tools/Makemodule.am
include policy/Makemodule.am
include data/Makemodule.am
#include po/Makemodule.am
include examples/Makemodule.am

#SUBDIRS += \
#	. \
#	clients \
#	tools \
#	policy \
#	data \
#	po \
#	examples

if WITH_LEGACY_LIBRARIES
include libnm-util/Makemodule.am
include libnm-glib/Makemodule.am
endif

#SUBDIRS += \
#	docs \
#	vapi

include vapi/Makemodule.am

@GNOME_CODE_COVERAGE_RULES@

@INTLTOOL_POLICY_RULE@

EXTRA_DIST += \
	CONTRIBUTING \
	NetworkManager.pc.in \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	Makefile.glib \
	autogen.sh \
	valgrind.suppressions \
	$(noinst_DATA)

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-tests=yes \
	--with-valgrind=no \
	--enable-gtk-doc \
	--enable-more-warnings=error \
	--with-udev-dir=$$dc_install_base/lib/udev \
	--with-wext=no \
	--enable-ifcfg-rh \
	--enable-ifupdown \
	--enable-ifnet

if BUILD_SETTING_DOCS
dist-check-setting-docs:
else
dist-check-setting-docs:
	@echo "*** gobject-introspection and pygobject are needed to run 'make dist'. ***"
	@false
endif

dist: dist-check-setting-docs

DISTCLEANFILES += \
	intltool-extract \
	intltool-merge \
	intltool-update \
	$(polkit_policy_DATA) \
	$(noinst_DATA)

CLEANFILES += \
	cscope.in.out \
	cscope.out \
	cscope.po.out \
	$(noinst_DATA) \
	$(dbusactivation_DATA) \
	$(gir_DATA) \
	$(typelib_DATA) \
	$(BUILT_SOURCES) \
	$(VAPIGEN_VAPIS)

pkgconfig_DATA += NetworkManager.pc

gir_DATA += $(INTROSPECTION_GIRS)

install-data-hook: $(INSTALL_DATA_HOOKS)
uninstall-hook: $(UNINSTALL_HOOKS)
check-local: $(CHECK_LOCALS)

cscope:
	cscope -b -q -R -sshared -ssrc -slibnm-core -slibnm -slibnm-glib -slibnm-util -sclients;

BUILT_SOURCES += $(GLIB_GENERATED)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

.PHONY: cscope
