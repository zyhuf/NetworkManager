/* NetworkManager -- Network link manager
 *
 * Tim Niemueller [www.niemueller.de]
 * Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 * (C) Copyright 2006 Tim Niemueller
 */

#ifndef NM_DBUS_DIALUP_H
#define NM_DBUS_DIALUP_H

#include "NetworkManagerDbusUtils.h"
#include "nm-dialup-manager.h"
#include "nm-dialup-connection.h"

void				nm_dbus_dialup_schedule_dialup_connections_update	(NMData *app_data);
void				nm_dbus_dialup_update_one_dialup_connection		(DBusConnection *connection, const char *dialup, NMData *data);

void				nm_dbus_dialup_signal_dialup_connection_update	(DBusConnection *con, NMDialupConnection *dialup, const char *signal);
void				nm_dbus_dialup_signal_dialup_failed				(DBusConnection *con, const char *signal, NMDialupConnection *dialup, const char *error_msg);
void				nm_dbus_dialup_signal_dialup_connection_state_change (DBusConnection *con, NMDialupConnection *dialup, NMDialupActStage new_stage);

NMDbusMethodList *	nm_dbus_dialup_methods_setup				(void);

#endif
