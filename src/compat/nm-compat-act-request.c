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

#include "config.h"

#include <glib.h>

#include "NetworkManager.h"
#include "nm-properties-changed-signal.h"
#include "nm-compat-act-request.h"

G_DEFINE_TYPE (NMCompatActRequest, nm_compat_act_request, NM_TYPE_COMPAT_ACTIVE_CONNECTION)

enum {
	PROPERTIES_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

/********************************************************************/

NMCompatActRequest *
nm_compat_act_request_new (NMActRequest *parent, DBusGConnection *bus)
{
	NMCompatActRequest *self;

	self = (NMCompatActRequest *) g_object_new (NM_TYPE_COMPAT_ACT_REQUEST,
	                                            "parent", parent,
	                                            NULL);
	if (self)
		nm_compat_active_connection_export (NM_COMPAT_ACTIVE_CONNECTION (self), bus);

	return self;
}

static void
nm_compat_act_request_init (NMCompatActRequest *self)
{
}

static NMConnection *
get_connection (NMCompatActiveConnection *compat, GObject *parent)
{
	/* Parent should be an NMActRequest */
	return nm_act_request_get_connection (NM_ACT_REQUEST (parent));
}

static NMDevice *
get_device (NMCompatActiveConnection *compat, GObject *parent)
{
	/* Parent should be an NMActRequest */
	return NM_DEVICE (nm_act_request_get_device (NM_ACT_REQUEST (parent)));
}

static void
nm_compat_act_request_class_init (NMCompatActRequestClass *compat_class)
{
	NMCompatActiveConnectionClass *ac_class = NM_COMPAT_ACTIVE_CONNECTION_CLASS (compat_class);

	ac_class->get_connection = get_connection;
	ac_class->get_device = get_device;

	signals[PROPERTIES_CHANGED] =
		nm_properties_changed_signal_new (G_OBJECT_CLASS (compat_class),
		                                  G_STRUCT_OFFSET (NMCompatActRequestClass, properties_changed));
}

