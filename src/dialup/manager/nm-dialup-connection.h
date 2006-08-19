/* nm-dialup-connection.h - handle a single dialup connection within NetworkManager's framework 
 *
 * Copyright (C) 2006 Tim Niemueller
 * base on work Copyright (C) 2005 Dan Williams
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */
#ifndef NM_DIALUP_CONNECTION_H
#define NM_DIALUP_CONNECTION_H

#include "nm-device.h"
#include "nm-named-manager.h"

typedef struct NMDialupConnection NMDialupConnection;


NMDialupConnection *		nm_dialup_connection_new		(const char *name, const char *user_name, const char *service_name,
									 NMNamedManager *named_manager, DBusConnection *dbus_connection);
void				nm_dialup_connection_ref		(NMDialupConnection *con);
void				nm_dialup_connection_unref		(NMDialupConnection *con);

const char *			nm_dialup_connection_get_name		(NMDialupConnection *con);
const char *			nm_dialup_connection_get_user_name	(NMDialupConnection *con);
const char *			nm_dialup_connection_get_service_name	(NMDialupConnection *con);

void				nm_dialup_connection_activate		(NMDialupConnection *con);
void				nm_dialup_connection_deactivate		(NMDialupConnection *con);

gboolean			nm_dialup_connection_set_config		(NMDialupConnection *con, const char *dialup_iface, NMIP4Config *ip4_config);

#endif  /* NM_DIALUP_CONNECTION_H */
