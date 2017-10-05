/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-ovsdb.h"
#include "nm-device-ovs-interface.h"
#include "nm-device-ovs-port.h"
#include "nm-device-ovs-bridge.h"
#include "platform/nm-platform.h"
#include "nm-core-internal.h"
#include "devices/nm-device-factory.h"

/*****************************************************************************/

typedef struct {
	NMDeviceFactory parent;
} NMOpenvswitchFactory;

typedef struct {
	NMDeviceFactoryClass parent;
} NMOpenvswitchFactoryClass;

#define NM_TYPE_OPENVSWITCH_FACTORY            (nm_openvswitch_factory_get_type ())
#define NM_OPENVSWITCH_FACTORY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_OPENVSWITCH_FACTORY, NMOpenvswitchFactory))
#define NM_OPENVSWITCH_FACTORY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_OPENVSWITCH_FACTORY, NMOpenvswitchFactoryClass))
#define NM_IS_OPENVSWITCH_FACTORY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_OPENVSWITCH_FACTORY))
#define NM_IS_OPENVSWITCH_FACTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_OPENVSWITCH_FACTORY))
#define NM_OPENVSWITCH_FACTORY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_OPENVSWITCH_FACTORY, NMOpenvswitchFactoryClass))

static GType nm_openvswitch_factory_get_type (void);
G_DEFINE_TYPE (NMOpenvswitchFactory, nm_openvswitch_factory, NM_TYPE_DEVICE_FACTORY)

/*****************************************************************************/

#define _NMLOG_DOMAIN      LOGD_DEVICE
#define _NMLOG(level, ...) __NMLOG_DEFAULT (level, _NMLOG_DOMAIN, "openvswitch", __VA_ARGS__)

/*****************************************************************************/

NM_DEVICE_FACTORY_DECLARE_TYPES (
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES    (NM_LINK_TYPE_OPENVSWITCH)
	NM_DEVICE_FACTORY_DECLARE_SETTING_TYPES (NM_SETTING_OVS_BRIDGE_SETTING_NAME,
	                                         NM_SETTING_OVS_INTERFACE_SETTING_NAME,
	                                         NM_SETTING_OVS_PORT_SETTING_NAME)
)

G_MODULE_EXPORT NMDeviceFactory *
nm_device_factory_create (GError **error)
{
	return (NMDeviceFactory *) g_object_new (NM_TYPE_OPENVSWITCH_FACTORY, NULL);
}

static NMDevice *
device_from_type (const char *name, const char *type)
{
	if (g_strcmp0 (type, NM_SETTING_OVS_INTERFACE_SETTING_NAME) == 0) {
		return g_object_new (NM_TYPE_DEVICE_OVS_INTERFACE,
		                     NM_DEVICE_IFACE, name,
		                     NM_DEVICE_DRIVER, "openvswitch",
		                     NM_DEVICE_TYPE_DESC, "OpenVSwitch Interface",
		                     NM_DEVICE_DEVICE_TYPE, NM_TYPE_DEVICE_OVS_INTERFACE,
	                             NM_DEVICE_LINK_TYPE, NM_LINK_TYPE_OPENVSWITCH,
		                     NULL);
	} else if (g_strcmp0 (type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		return g_object_new (NM_TYPE_DEVICE_OVS_PORT,
		                     NM_DEVICE_IFACE, name,
		                     NM_DEVICE_DRIVER, "openvswitch",
		                     NM_DEVICE_TYPE_DESC, "OpenVSwitch Port",
		                     NM_DEVICE_DEVICE_TYPE, NM_TYPE_DEVICE_OVS_PORT,
		                     NULL);
	} else if (g_strcmp0 (type, NM_SETTING_OVS_BRIDGE_SETTING_NAME) == 0) {
		return g_object_new (NM_TYPE_DEVICE_OVS_BRIDGE,
		                     NM_DEVICE_IFACE, name,
		                     NM_DEVICE_DRIVER, "openvswitch",
		                     NM_DEVICE_TYPE_DESC, "OpenVSwitch Bridge",
		                     NM_DEVICE_DEVICE_TYPE, NM_TYPE_DEVICE_OVS_BRIDGE,
		                     NULL);
	}

	return NULL;
}

static void
ovsdb_device_added (NMOvsdb *ovsdb, const char *type, const char *name,
                    NMDeviceFactory *self)
{
	NMDevice *device = NULL;
	g_printerr ("ADDED: [%s] %s\n", type, name);


	device = device_from_type (name, type);
	g_return_if_fail (device);

	g_signal_emit_by_name (self, NM_DEVICE_FACTORY_DEVICE_ADDED, device);
	g_object_unref (device);
}

static void
ovsdb_device_removed (NMOvsdb *ovsdb, const char *type, const char *name,
                      NMDeviceFactory *self)
{
	g_printerr ("REMOVED: [%s] %s\n", type, name);
}

static void
start (NMDeviceFactory *self)
{
	NMOvsdb *ovsdb;

	ovsdb = nm_ovsdb_get ();

	g_signal_connect (ovsdb, NM_OVSDB_DEVICE_ADDED, G_CALLBACK (ovsdb_device_added), self);
	g_signal_connect (ovsdb, NM_OVSDB_DEVICE_REMOVED, G_CALLBACK (ovsdb_device_removed), self);
}

static NMDevice *
create_device (NMDeviceFactory *self,
               const char *iface,
               const NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	if (g_strcmp0 (iface, "ovs-system") == 0)
		return NULL;

	g_printerr ("CREATE DEVICE [%s]\n", iface);

	return device_from_type (iface, nm_connection_get_connection_type (connection));
#if 0
	return (NMDevice *) g_object_new (NM_TYPE_DEVICE_OPENVSWITCH,
	                                  NM_DEVICE_IFACE, iface,
	                                  NM_DEVICE_TYPE_DESC, "OpenVSwitch",
	                                  NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_OPENVSWITCH,
	                                  NM_DEVICE_LINK_TYPE, NM_LINK_TYPE_OPENVSWITCH,
	                                  NULL);
#else
#endif
}

static void
nm_openvswitch_factory_init (NMOpenvswitchFactory *self)
{
}

static void
nm_openvswitch_factory_class_init (NMOpenvswitchFactoryClass *klass)
{
	NMDeviceFactoryClass *factory_class = NM_DEVICE_FACTORY_CLASS (klass);

	factory_class->get_supported_types = get_supported_types;
	factory_class->start = start;
	factory_class->create_device = create_device;
}
