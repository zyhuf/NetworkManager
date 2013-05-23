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
 * Copyright (C) 2009 Novell, Inc.
 * Copyright (C) 2013 Red Hat, Inc.
 */

#include "nm-hostname-provider.h"

G_DEFINE_INTERFACE (NMHostnameProvider, nm_hostname_provider, G_TYPE_OBJECT)

const char *
nm_hostname_provider_get_hostname (NMHostnameProvider *self)
{
	g_return_val_if_fail (NM_IS_HOSTNAME_PROVIDER (self), NULL);

	return NM_HOSTNAME_PROVIDER_GET_INTERFACE (self)->get_hostname (self);
}

static void
nm_hostname_provider_default_init (NMHostnameProviderInterface *iface)
{
}
