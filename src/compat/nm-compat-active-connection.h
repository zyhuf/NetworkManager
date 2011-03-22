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

#ifndef NM_COMPAT_ACTIVE_CONNECTION_H
#define NM_COMPAT_ACTIVE_CONNECTION_H

#include <glib.h>
#include <glib-object.h>
#include "nm-activation-request.h"

#define NM_TYPE_COMPAT_ACTIVE_CONNECTION            (nm_compat_active_connection_get_type ())
#define NM_COMPAT_ACTIVE_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_COMPAT_ACTIVE_CONNECTION, NMCompatActiveConnection))
#define NM_COMPAT_ACTIVE_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_COMPAT_ACTIVE_CONNECTION, NMCompatActiveConnectionClass))
#define NM_IS_COMPAT_ACTIVE_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_COMPAT_ACTIVE_CONNECTION))
#define NM_IS_COMPAT_ACTIVE_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_COMPAT_ACTIVE_CONNECTION))
#define NM_COMPAT_ACTIVE_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_COMPAT_ACTIVE_CONNECTION, NMCompatActiveConnectionClass))

typedef struct {
	GObject parent;
} NMCompatActiveConnection;

typedef struct {
	GObjectClass parent;

	void (*properties_changed) (NMCompatActiveConnection *self, GHashTable *properties);
} NMCompatActiveConnectionClass;

GType nm_compat_active_connection_get_type (void);

NMCompatActiveConnection *nm_compat_active_connection_new (NMActRequest *parent, DBusGConnection *bus);

const char *nm_compat_active_connection_get_path (NMCompatActiveConnection *self);

#endif /* NM_COMPAT_ACTIVE_CONNECTION_H */
