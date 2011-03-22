/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkDevice -- Network link manager
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

#ifndef NM_COMPAT_DEVICE_H
#define NM_COMPAT_DEVICE_H

#include <glib.h>
#include <glib-object.h>
#include "nm-device.h"

#define NM_TYPE_COMPAT_DEVICE            (nm_compat_device_get_type ())
#define NM_COMPAT_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_COMPAT_DEVICE, NMCompatDevice))
#define NM_COMPAT_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_COMPAT_DEVICE, NMCompatDeviceClass))
#define NM_IS_COMPAT_DEVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_COMPAT_DEVICE))
#define NM_IS_COMPAT_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_COMPAT_DEVICE))
#define NM_COMPAT_DEVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_COMPAT_DEVICE, NMCompatDeviceClass))

typedef struct {
	GObject parent;
} NMCompatDevice;

typedef struct {
	GObjectClass parent;

	void (*properties_changed) (NMCompatDevice *self, GHashTable *properties);
} NMCompatDeviceClass;

GType nm_compat_device_get_type (void);

NMCompatDevice *nm_compat_device_new (NMDevice *parent);

void nm_compat_device_export (NMCompatDevice *compat, DBusGConnection *bus);

const char *nm_compat_device_get_path (NMCompatDevice *compat);

#endif /* NM_COMPAT_DEVICE_H */
