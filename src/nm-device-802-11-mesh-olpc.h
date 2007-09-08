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

#ifndef NM_DEVICE_802_11_MESH_OLPC_H
#define NM_DEVICE_802_11_MESH_OLPC_H

#include <glib.h>
#include <glib-object.h>
#include <net/ethernet.h>

#include "nm-device.h"

G_BEGIN_DECLS

#define NM_TYPE_DEVICE_802_11_MESH_OLPC			(nm_device_802_11_mesh_olpc_get_type ())
#define NM_DEVICE_802_11_MESH_OLPC(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DEVICE_802_11_MESH_OLPC, NMDevice80211MeshOLPC))
#define NM_DEVICE_802_11_MESH_OLPC_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  NM_TYPE_DEVICE_802_11_MESH_OLPC, NMDevice80211MeshOLPCClass))
#define NM_IS_DEVICE_802_11_MESH_OLPC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DEVICE_802_11_MESH_OLPC))
#define NM_IS_DEVICE_802_11_MESH_OLPC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  NM_TYPE_DEVICE_802_11_MESH_OLPC))
#define NM_DEVICE_802_11_MESH_OLPC_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  NM_TYPE_DEVICE_802_11_MESH_OLPC, NMDevice80211MeshOLPCClass))

#ifndef NM_DEVICE_802_11_MESH_OLPC_DEFINED
#define NM_DEVICE_802_11_MESH_OLPC_DEFINED
typedef struct _NMDevice80211MeshOLPC NMDevice80211MeshOLPC;
#endif

typedef struct _NMDevice80211MeshOLPCClass NMDevice80211MeshOLPCClass;
typedef struct _NMDevice80211MeshOLPCPrivate NMDevice80211MeshOLPCPrivate;

struct _NMDevice80211MeshOLPC
{
	NMDevice parent;

	/*< private >*/
	NMDevice80211MeshOLPCPrivate *priv;
};

struct _NMDevice80211MeshOLPCClass
{
	NMDeviceClass parent;
};


GType nm_device_802_11_mesh_olpc_get_type (void);


static inline gboolean nm_device_is_802_11_mesh_olpc (NMDevice *dev);
static inline gboolean nm_device_is_802_11_mesh_olpc (NMDevice *dev)
{
	g_return_val_if_fail (dev != NULL, FALSE);

	return (G_OBJECT_TYPE (dev) == NM_TYPE_DEVICE_802_11_MESH_OLPC);
}

struct NMData;
void nm_get_anycast_addrs (struct NMData *data);

double nm_device_802_11_mesh_olpc_get_frequency (NMDevice80211MeshOLPC *self);

guint32 nm_device_802_11_mesh_olpc_parse_mesh_step (const char * step_string,
                                                    guint32 fallback);

#endif	/* NM_DEVICE_802_11_MESH_OLPC_H */
