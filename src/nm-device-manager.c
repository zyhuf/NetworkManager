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

#include "nm-device-manager.h"

G_DEFINE_INTERFACE (NMDeviceManager, nm_device_manager, G_TYPE_OBJECT)

/**
 * nm_device_manager_get_devices:
 * @self: the #NMDeviceManager
 *
 * Returns: a #GSList of #NMDevice objects representing all known
 *   devices.  Returned list is owned by the device manager and must
 *   not be freed.
 */
const GSList *
nm_device_manager_get_devices (NMDeviceManager *self)
{
	g_return_val_if_fail (NM_IS_DEVICE_MANAGER (self), NULL);

	if (NM_DEVICE_MANAGER_GET_INTERFACE (self)->get_devices)
		return NM_DEVICE_MANAGER_GET_INTERFACE (self)->get_devices (self);
	return NULL;
}

/*****************************************************************************/

static void
nm_device_manager_default_init (NMDeviceManagerInterface *iface)
{
	GType iface_type = G_TYPE_FROM_INTERFACE (iface);

	/* Signals */
	g_signal_new (NM_DM_SIGNAL_DEVICE_ADDED,
	              iface_type,
	              G_SIGNAL_RUN_FIRST,
	              G_STRUCT_OFFSET (NMDeviceManagerInterface, device_added),
	              NULL, NULL,
	              g_cclosure_marshal_VOID__OBJECT,
	              G_TYPE_NONE, 1, NM_TYPE_DEVICE);

	g_signal_new (NM_DM_SIGNAL_DEVICE_REMOVED,
	              iface_type,
	              G_SIGNAL_RUN_FIRST,
	              G_STRUCT_OFFSET (NMDeviceManagerInterface, device_removed),
	              NULL, NULL,
	              g_cclosure_marshal_VOID__OBJECT,
	              G_TYPE_NONE, 1, NM_TYPE_DEVICE);
}
