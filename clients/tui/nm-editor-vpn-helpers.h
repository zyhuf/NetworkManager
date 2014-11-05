/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2013-2014 Red Hat, Inc.
 */

#ifndef _NM_EDITOR_VPN_HELPERS_H_
#define _NM_EDITOR_VPN_HELPERS_H_

#include <NetworkManager.h>

GHashTable *nm_editor_get_vpn_plugins (GError **error);

NMVpnEditorPlugin *nm_editor_get_vpn_plugin_by_service (const char *service, GError **erro);

typedef void (*NMEditorVpnImportSuccessCallback) (NMConnection *connection, gpointer user_data);
void nm_editor_vpn_import (NMEditorVpnImportSuccessCallback callback, gpointer user_data);

void nm_editor_vpn_export (NMConnection *connection);

gboolean nm_editor_vpn_supports_ipv6 (NMConnection *connection);

#endif  /* _NM_EDITOR_VPN_HELPERS_H_ */
