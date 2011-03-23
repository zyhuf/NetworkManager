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
#include <stdio.h>
#include <string.h>

#include "NetworkManager.h"
#include "nm-compat-device-gsm.h"
#include "nm-properties-changed-signal.h"

G_DEFINE_TYPE (NMCompatDeviceGsm, nm_compat_device_gsm, NM_TYPE_COMPAT_DEVICE)

enum {
	PROPERTIES_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

#include "nm-compat-device-gsm-glue.h"

/*************************************************************************/

NMCompatDeviceGsm *
nm_compat_device_gsm_new (NMDeviceModem *parent)
{
	return (NMCompatDeviceGsm *) g_object_new (NM_TYPE_COMPAT_DEVICE_GSM,
	                                           NM_COMPAT_DEVICE_PARENT, parent,
	                                           NULL);
}

static void
nm_compat_device_gsm_init (NMCompatDeviceGsm *self)
{
}

static void
nm_compat_device_gsm_class_init (NMCompatDeviceGsmClass *compat_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (compat_class);

	signals[PROPERTIES_CHANGED] =
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMCompatDeviceGsmClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (compat_class), &dbus_glib_nm_compat_device_gsm_object_info);
}

