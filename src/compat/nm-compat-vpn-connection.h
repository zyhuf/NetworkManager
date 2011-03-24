/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#ifndef NM_COMPAT_VPN_CONNECTION_H
#define NM_COMPAT_VPN_CONNECTION_H

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "nm-vpn-connection.h"
#include "nm-compat-active-connection.h"

#define NM_TYPE_COMPAT_VPN_CONNECTION            (nm_compat_vpn_connection_get_type ())
#define NM_COMPAT_VPN_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_COMPAT_VPN_CONNECTION, NMCompatVpnConnection))
#define NM_COMPAT_VPN_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_COMPAT_VPN_CONNECTION, NMCompatVpnConnectionClass))
#define NM_IS_COMPAT_VPN_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_COMPAT_VPN_CONNECTION))
#define NM_IS_COMPAT_VPN_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_COMPAT_VPN_CONNECTION))
#define NM_COMPAT_VPN_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_COMPAT_VPN_CONNECTION, NMCompatVpnConnectionClass))

typedef struct {
	NMCompatActiveConnection parent;
} NMCompatVpnConnection;

typedef struct {
	NMCompatActiveConnectionClass parent;

	void (*vpn_state_changed) (NMCompatVpnConnection *self,
	                           NMVPNConnectionState state,
	                           NMVPNConnectionStateReason reason);

	void (*properties_changed) (NMCompatVpnConnection *self, GHashTable *properties);
} NMCompatVpnConnectionClass;

GType nm_compat_vpn_connection_get_type (void);

NMCompatVpnConnection *nm_compat_vpn_connection_new (NMVPNConnection *parent, DBusGConnection *bus);

#endif /* NM_COMPAT_VPN_CONNECTION_H */
