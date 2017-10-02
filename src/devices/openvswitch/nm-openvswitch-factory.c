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
	                                         NM_SETTING_OVS_PORT_SETTING_NAME)
)

G_MODULE_EXPORT NMDeviceFactory *
nm_device_factory_create (GError **error)
{
	return (NMDeviceFactory *) g_object_new (NM_TYPE_OPENVSWITCH_FACTORY, NULL);
}

static void
start (NMDeviceFactory *factory)
{
	nm_ovsdb_get ();
}

static NMDevice *
create_device (NMDeviceFactory *factory,
               const char *iface,
               const NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	if (g_strcmp0 (iface, "ovs-system") == 0)
		return NULL;

	return NULL;
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
