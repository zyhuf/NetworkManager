/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <glib.h>
#include <string.h>
#include <netinet/in.h>

#include "nm-device.h"
#include "nm-device-802-11-mesh-olpc.h"
#include "nm-device-private.h"
#include "NetworkManagerUtils.h"
#include "nm-device-802-11-wireless.h"
#include "nm-utils.h"

#define NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_802_11_MESH_OLPC, NMDevice80211MeshOLPCPrivate))

struct _NMDevice80211MeshOLPCPrivate
{
	gboolean	dispose_has_run;
	gboolean	is_initialized;

	guint32		capabilities;

	NMDevice80211Wireless * ethdev;
};


static void
nm_device_802_11_mesh_olpc_init (NMDevice80211MeshOLPC * self)
{
	self->priv = NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE (self);
	self->priv->dispose_has_run = FALSE;
	self->priv->is_initialized = FALSE;
}

static void
real_init (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	self->priv->is_initialized = TRUE;

	self->priv->capabilities = 0;
}

static void
real_start (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData * data = nm_device_get_app_data (dev);
	const char * mesh_physdev = nm_device_get_physical_device_udi (dev);
	NMDevice * found_ethdev = NULL;
	GSList * elt;

	g_assert (mesh_physdev);

	/* Find the 802.11 non-mesh device if HAL knows about it already */
	nm_lock_mutex (data->dev_list_mutex, __FUNCTION__);

	for (elt = data->dev_list; elt != NULL; elt = g_slist_next (elt)) {
		NMDevice * eth_dev = (NMDevice *)(elt->data);
		const char * eth_physdev;

		if (!nm_device_is_802_11_wireless (eth_dev))
			continue;
		eth_physdev = nm_device_get_physical_device_udi (eth_dev);
		if (!eth_physdev)
			continue;
		if (strcmp (mesh_physdev, eth_physdev) == 0) {
			found_ethdev = eth_dev;
			break;
		}
	}

	if (found_ethdev) {
		g_object_ref (found_ethdev);
		self->priv->ethdev = NM_DEVICE_802_11_WIRELESS (found_ethdev);
		nm_info ("%s: found 802.11 companion device %s.", nm_device_get_iface (dev),
				nm_device_get_iface (found_ethdev));
	}

	nm_unlock_mutex (data->dev_list_mutex, __FUNCTION__);
}

static void
real_notify_device_added (NMDevice *dev,
                          NMDevice *added_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	const char * mesh_physdev = nm_device_get_physical_device_udi (dev);
	const char * eth_physdev = NULL;

	if (dev == added_dev)
		return;

	if (self->priv->ethdev || !nm_device_is_802_11_wireless (added_dev))
		return;

	eth_physdev = nm_device_get_physical_device_udi (added_dev);
	if (!eth_physdev)
		return;

	if (strcmp (mesh_physdev, eth_physdev) == 0) {
		g_object_ref (added_dev);
		self->priv->ethdev = NM_DEVICE_802_11_WIRELESS (added_dev);
		nm_info ("%s: found 802.11 companion device %s.", nm_device_get_iface (dev),
				nm_device_get_iface (added_dev));
	}
}

static void
real_notify_device_removed (NMDevice *dev,
                            NMDevice *removed_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	if (dev == removed_dev)
		return;

	if (!self->priv->ethdev)
		return;

	if (   !nm_device_is_802_11_wireless (removed_dev)
	    || (self->priv->ethdev != NM_DEVICE_802_11_WIRELESS (removed_dev)))
		return;

	nm_info ("%s: 802.11 companion device %s removed.", nm_device_get_iface (dev),
			nm_device_get_iface (removed_dev));
	self->priv->ethdev = NULL;
	g_object_unref (removed_dev);

	/* FIXME: ethdev gone, need state change */
}

static void
real_deactivate_quickly (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
}


static void
real_deactivate (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
}

static guint32
real_get_generic_capabilities (NMDevice *dev)
{
	return NM_DEVICE_CAP_NM_SUPPORTED;
}

static guint32
real_get_type_capabilities (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	return self->priv->capabilities;
}

static void
nm_device_802_11_mesh_olpc_dispose (GObject *object)
{
	NMDevice80211MeshOLPC *		self = NM_DEVICE_802_11_MESH_OLPC (object);
	NMDevice80211MeshOLPCClass *	klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (object);
	NMDeviceClass *			parent_class;

	/* Make sure dispose does not run twice. */
	if (self->priv->dispose_has_run)
		return;

	self->priv->dispose_has_run = TRUE;

	/* Only do this part of the cleanup if the object is initialized */
	if (self->priv->is_initialized) {
	}

	/* Chain up to the parent class */
	parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
nm_device_802_11_mesh_olpc_finalize (GObject *object)
{
	NMDevice80211MeshOLPCClass *	klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (object);
	NMDeviceClass *			parent_class;

	/* Chain up to the parent class */
	parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
nm_device_802_11_mesh_olpc_class_init (NMDevice80211MeshOLPCClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *parent_class = NM_DEVICE_CLASS (klass);

	object_class->dispose = nm_device_802_11_mesh_olpc_dispose;
	object_class->finalize = nm_device_802_11_mesh_olpc_finalize;

	parent_class->get_type_capabilities = real_get_type_capabilities;
	parent_class->get_generic_capabilities = real_get_generic_capabilities;
	parent_class->init = real_init;
	parent_class->start = real_start;
	parent_class->deactivate = real_deactivate;
	parent_class->deactivate_quickly = real_deactivate_quickly;
	parent_class->notify_device_added = real_notify_device_added;
	parent_class->notify_device_removed = real_notify_device_removed;

	g_type_class_add_private (object_class, sizeof (NMDevice80211MeshOLPCPrivate));
}

GType
nm_device_802_11_mesh_olpc_get_type (void)
{
	static GType type = 0;
	if (type == 0)
	{
		static const GTypeInfo info =
		{
			sizeof (NMDevice80211MeshOLPCClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) nm_device_802_11_mesh_olpc_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (NMDevice80211MeshOLPC),
			0,		/* n_preallocs */
			(GInstanceInitFunc) nm_device_802_11_mesh_olpc_init,
			NULL		/* value_table */
		};
		type = g_type_register_static (NM_TYPE_DEVICE,
					       "NMDevice80211MeshOLPC",
					       &info, 0);
	}
	return type;
}
