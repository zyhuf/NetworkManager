/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2018 Javier Arteaga <jarteaga@jbeta.is>
 */

#include "nm-default.h"

#include "nm-device-wireguard.h"

#include "nm-device-private.h"
#include "platform/nm-platform.h"
#include "nm-device-factory.h"

#include "nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceWireguard);

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE (NMDeviceWireguard,
	PROP_PRIVATE_KEY,
	PROP_PUBLIC_KEY,
	PROP_LISTEN_PORT,
	PROP_FWMARK,
);

struct _NMDeviceWireguard {
	NMDevice parent;
	NMPlatformLnkWireguard props;
};

struct _NMDeviceWireguardClass {
	NMDeviceClass parent;
};

G_DEFINE_TYPE (NMDeviceWireguard, nm_device_wireguard, NM_TYPE_DEVICE)

/******************************************************************/

static void
update_properties (NMDevice *device)
{
	NMDeviceWireguard *self;
	const NMPlatformLink *plink;
	const NMPlatformLnkWireguard *props = NULL;
	int ifindex;

	g_return_if_fail (NM_IS_DEVICE_WIREGUARD (device));
	self = NM_DEVICE_WIREGUARD (device);

	ifindex = nm_device_get_ifindex (device);
	props = nm_platform_link_get_lnk_wireguard (nm_device_get_platform (device), ifindex, &plink);
	if (!props) {
		_LOGW (LOGD_PLATFORM, "could not get wireguard properties");
		return;
	}

	g_object_freeze_notify (G_OBJECT (device));

#define CHECK_PROPERTY_CHANGED(field, prop) \
	G_STMT_START { \
		if (self->props.field != props->field) { \
			self->props.field = props->field; \
			_notify (self, prop); \
		} \
	} G_STMT_END

#define CHECK_PROPERTY_CHANGED_ARRAY(field, prop) \
	G_STMT_START { \
		if (memcmp (&self->props.field, &props->field, sizeof (props->field)) != 0) { \
			memcpy (&self->props.field, &props->field, sizeof (props->field)); \
			_notify (self, prop); \
		} \
	} G_STMT_END

	CHECK_PROPERTY_CHANGED_ARRAY (private_key, PROP_PRIVATE_KEY);
	CHECK_PROPERTY_CHANGED_ARRAY (public_key, PROP_PUBLIC_KEY);
	CHECK_PROPERTY_CHANGED (listen_port, PROP_LISTEN_PORT);
	CHECK_PROPERTY_CHANGED (fwmark, PROP_FWMARK);

	g_object_thaw_notify (G_OBJECT (device));
}

static void
link_changed (NMDevice *device,
              const NMPlatformLink *pllink)
{
	NM_DEVICE_CLASS (nm_device_wireguard_parent_class)->link_changed (device, pllink);
	update_properties (device);
}


/******************************************************************/

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDeviceWireguard *self = NM_DEVICE_WIREGUARD (object);
	gchar *b64;

	switch (prop_id) {
	case PROP_PRIVATE_KEY:
		b64 = g_base64_encode (self->props.private_key, sizeof (self->props.private_key));
		g_value_set_string (value, b64);
		g_free (b64);
		break;
	case PROP_PUBLIC_KEY:
		b64 = g_base64_encode (self->props.public_key, sizeof (self->props.public_key));
		g_value_set_string (value, b64);
		g_free (b64);
		break;
	case PROP_LISTEN_PORT:
		g_value_set_uint (value, self->props.listen_port);
		break;
	case PROP_FWMARK:
		g_value_set_uint (value, self->props.fwmark);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_wireguard_init (NMDeviceWireguard *self)
{
}

static const NMDBusInterfaceInfoExtended interface_info_device_wireguard = {
       .parent = NM_DEFINE_GDBUS_INTERFACE_INFO_INIT (
               NM_DBUS_INTERFACE_DEVICE_WIREGUARD,
               .properties = NM_DEFINE_GDBUS_PROPERTY_INFOS (
                       NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE ("PublicKey", "s", NM_DEVICE_WIREGUARD_PUBLIC_KEY),
                       NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE ("ListenPort", "q", NM_DEVICE_WIREGUARD_LISTEN_PORT),
                       NM_DEFINE_DBUS_PROPERTY_INFO_EXTENDED_READABLE ("FwMark", "u", NM_DEVICE_WIREGUARD_FWMARK),
               ),
       ),
};

static void
nm_device_wireguard_class_init (NMDeviceWireguardClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDBusObjectClass *dbus_object_class = NM_DBUS_OBJECT_CLASS (klass);
	NMDeviceClass *device_class = NM_DEVICE_CLASS (klass);

	NM_DEVICE_CLASS_DECLARE_TYPES (klass, NULL, NM_LINK_TYPE_WIREGUARD)

	object_class->get_property = get_property;

	dbus_object_class->interface_infos = NM_DBUS_INTERFACE_INFOS (&interface_info_device_wireguard);

	device_class->link_changed = link_changed;

	obj_properties[PROP_PRIVATE_KEY] =
	    g_param_spec_string (NM_DEVICE_WIREGUARD_PRIVATE_KEY,
	                         "", "",
	                         NULL,
	                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_PUBLIC_KEY] =
	    g_param_spec_string (NM_DEVICE_WIREGUARD_PUBLIC_KEY,
	                         "", "",
	                         NULL,
	                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_LISTEN_PORT] =
	    g_param_spec_uint (NM_DEVICE_WIREGUARD_LISTEN_PORT,
	                       "", "",
	                       0, G_MAXUINT16, 0,
	                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_FWMARK] =
	    g_param_spec_uint (NM_DEVICE_WIREGUARD_FWMARK,
	                       "", "",
	                       0, G_MAXUINT32, 0,
	                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, _PROPERTY_ENUMS_LAST, obj_properties);
}

/*************************************************************/

#define NM_TYPE_WIREGUARD_DEVICE_FACTORY (nm_wireguard_device_factory_get_type ())
#define NM_WIREGUARD_DEVICE_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_WIREGUARD_DEVICE_FACTORY, NMWireguardDeviceFactory))

static NMDevice *
create_device (NMDeviceFactory *factory,
               const char *iface,
               const NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	return (NMDevice *) g_object_new (NM_TYPE_DEVICE_WIREGUARD,
	                                  NM_DEVICE_IFACE, iface,
	                                  NM_DEVICE_TYPE_DESC, "WireGuard",
	                                  NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_WIREGUARD,
	                                  NM_DEVICE_LINK_TYPE, NM_LINK_TYPE_WIREGUARD,
	                                  NULL);
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL (WIREGUARD, Wireguard, wireguard,
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES (NM_LINK_TYPE_WIREGUARD),
	factory_class->create_device = create_device;
)
