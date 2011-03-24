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

#ifndef NM_COMPAT_ACT_REQUEST_H
#define NM_COMPAT_ACT_REQUEST_H

#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "nm-activation-request.h"
#include "nm-compat-active-connection.h"

#define NM_TYPE_COMPAT_ACT_REQUEST            (nm_compat_act_request_get_type ())
#define NM_COMPAT_ACT_REQUEST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_COMPAT_ACT_REQUEST, NMCompatActRequest))
#define NM_COMPAT_ACT_REQUEST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_COMPAT_ACT_REQUEST, NMCompatActRequestClass))
#define NM_IS_COMPAT_ACT_REQUEST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_COMPAT_ACT_REQUEST))
#define NM_IS_COMPAT_ACT_REQUEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_COMPAT_ACT_REQUEST))
#define NM_COMPAT_ACT_REQUEST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_COMPAT_ACT_REQUEST, NMCompatActRequestClass))

typedef struct {
	NMCompatActiveConnection parent;
} NMCompatActRequest;

typedef struct {
	NMCompatActiveConnectionClass parent;

	void (*properties_changed) (NMCompatActRequest *self, GHashTable *properties);
} NMCompatActRequestClass;

GType nm_compat_act_request_get_type (void);

NMCompatActRequest *nm_compat_act_request_new (NMActRequest *parent, DBusGConnection *bus);

const char *nm_compat_act_request_get_path (NMCompatActRequest *self);

#endif /* NM_COMPAT_ACT_REQUEST_H */
