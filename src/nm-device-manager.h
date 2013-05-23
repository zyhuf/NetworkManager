/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright 2013 Red Hat, Inc.
 */

#ifndef NM_DEVICE_MANAGER_H
#define NM_DEVICE_MANAGER_H

#include <glib-object.h>
#include <nm-device.h>

#define NM_TYPE_DEVICE_MANAGER      (nm_device_manager_get_type ())
#define NM_DEVICE_MANAGER(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DEVICE_MANAGER, NMDeviceManager))
#define NM_IS_DEVICE_MANAGER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DEVICE_MANAGER))
#define NM_DEVICE_MANAGER_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), NM_TYPE_DEVICE_MANAGER, NMDeviceManagerInterface))

typedef struct _NMDeviceManager NMDeviceManager;
typedef struct _NMDeviceManagerInterface NMDeviceManagerInterface;

#define NM_DM_SIGNAL_DEVICE_ADDED        "dm-device-added"
#define NM_DM_SIGNAL_DEVICE_REMOVED      "dm-device-removed"

struct _NMDeviceManagerInterface {
	GTypeInterface g_iface;

	/* Methods */
	const GSList * (*get_devices) (NMDeviceManager *self);

	/* Signals */
	void (*device_added)   (NMDeviceManager *self, NMDevice *device);
	void (*device_removed) (NMDeviceManager *self, NMDevice *device, gboolean quitting);
};

GType nm_device_manager_get_type (void);

NMDeviceManager *nm_device_manager_get (void);

const GSList *nm_device_manager_get_devices (NMDeviceManager *self);

#endif /* NM_DEVICE_MANAGER_H */
