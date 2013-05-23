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
 * Copyright (C) 2012 Red Hat, Inc.
 */

#include "nm-connection-provider.h"
#include "nm-utils.h"

G_DEFINE_INTERFACE (NMConnectionProvider, nm_connection_provider, G_TYPE_OBJECT)

/**
 * nm_connection_provider_get_best_connections:
 * @self: the #NMConnectionProvider
 * @max_requested: if non-zero, the maximum number of connections to return
 * @ctype1: an #NMSetting base type (eg NM_SETTING_WIRELESS_SETTING_NAME) to
 *   filter connections against
 * @ctype2: a second #NMSetting base type (eg NM_SETTING_WIRELESS_SETTING_NAME)
 *   to filter connections against
 * @func: caller-supplied function for filtering connections
 * @func_data: caller-supplied data passed to @func
 *
 * Returns: a #GSList of #NMConnection objects in sorted order representing the
 *   "best" or highest-priority connections filtered by @ctype1 and/or @ctype2,
 *   and/or @func.  Caller is responsible for freeing the returned #GSList, but
 *   the contained values do not need to be unreffed.
 */
GSList *
nm_connection_provider_get_best_connections (NMConnectionProvider *self,
                                             guint max_requested,
                                             const char *ctype1,
                                             const char *ctype2,
                                             NMConnectionFilterFunc func,
                                             gpointer func_data)
{
	g_return_val_if_fail (NM_IS_CONNECTION_PROVIDER (self), NULL);

	if (NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->get_best_connections)
		return NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->get_best_connections (self, max_requested, ctype1, ctype2, func, func_data);
	return NULL;
}

/**
 * nm_connection_provider_get_connections:
 * @self: the #NMConnectionProvider
 *
 * Returns: a #GSList of #NMConnection objects representing all known
 *   connections.  Returned list is owned by the connection provider and must
 *   not be freed.
 */
const GSList *
nm_connection_provider_get_connections (NMConnectionProvider *self)
{
	g_return_val_if_fail (NM_IS_CONNECTION_PROVIDER (self), NULL);

	if (NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->get_connections)
		return NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->get_connections (self);
	return NULL;
}

/**
 * nm_connection_provider_add_connection:
 * @self: the #NMConnectionProvider
 * @connection: the source connection to create a new #NMSettingsConnection from
 * @save_to_disk: %TRUE to save the connection to disk immediately, %FALSE to
 * not save to disk
 * @error: on return, a location to store any errors that may occur
 *
 * Creates a new #NMSettingsConnection for the given source @connection.  
 * The plugin owns the returned object and the caller must reference the object
 * to continue using it.
 *
 * Returns: the new #NMSettingsConnection or %NULL
 */
NMConnection *
nm_connection_provider_add_connection (NMConnectionProvider *self,
                                       NMConnection *connection,
                                       gboolean save_to_disk,
                                       GError **error)
{
	g_return_val_if_fail (NM_IS_CONNECTION_PROVIDER (self), NULL);

	g_assert (NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->add_connection);
	return NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->add_connection (self, connection, save_to_disk, error);
}

/**
 * nm_connection_provider_get_connection_by_uuid:
 * @self: the #NMConnectionProvider
 * @uuid: the UUID to search for
 *
 * Returns: the connection with the given @uuid, or %NULL
 */
NMConnection *
nm_connection_provider_get_connection_by_uuid (NMConnectionProvider *self,
                                               const char *uuid)
{
	g_return_val_if_fail (NM_IS_CONNECTION_PROVIDER (self), NULL);
	g_return_val_if_fail (uuid != NULL, NULL);
	g_return_val_if_fail (nm_utils_is_uuid (uuid), NULL);

	g_assert (NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->get_connection_by_uuid);
	return NM_CONNECTION_PROVIDER_GET_INTERFACE (self)->get_connection_by_uuid (self, uuid);
}

/*****************************************************************************/

static void
nm_connection_provider_default_init (NMConnectionProviderInterface *iface)
{
	GType iface_type = G_TYPE_FROM_INTERFACE (iface);

	/* Signals */
	g_signal_new (NM_CP_SIGNAL_CONNECTION_ADDED,
	              iface_type,
	              G_SIGNAL_RUN_FIRST,
	              G_STRUCT_OFFSET (NMConnectionProviderInterface, connection_added),
	              NULL, NULL,
	              g_cclosure_marshal_VOID__OBJECT,
	              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	g_signal_new (NM_CP_SIGNAL_CONNECTION_UPDATED,
	              iface_type,
	              G_SIGNAL_RUN_FIRST,
	              G_STRUCT_OFFSET (NMConnectionProviderInterface, connection_updated),
	              NULL, NULL,
	              g_cclosure_marshal_VOID__OBJECT,
	              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	g_signal_new (NM_CP_SIGNAL_CONNECTION_REMOVED,
	              iface_type,
	              G_SIGNAL_RUN_FIRST,
	              G_STRUCT_OFFSET (NMConnectionProviderInterface, connection_removed),
	              NULL, NULL,
	              g_cclosure_marshal_VOID__OBJECT,
	              G_TYPE_NONE, 1, G_TYPE_OBJECT);
}
