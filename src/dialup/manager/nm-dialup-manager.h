/* nm-dialup-manager.h - handle dialup connections within NetworkManager's framework 
 *
 * Copyright (C) 2006 Tim Niemueller
 * based on work Copyright (C) 2005 Dan Williams
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
#ifndef NM_DIALUP_MANAGER_H
#define NM_DIALUP_MANAGER_H

#include <dbus/dbus.h>
#include "NetworkManagerMain.h"
#include "nm-dialup-connection.h"
#include "nm-dialup-service.h"


NMDialupManager *		nm_dialup_manager_new						(NMData *app_data);
NMDialupConnection *	nm_dialup_manager_add_connection				(NMDialupManager *manager, const char *name, const char *service_name, const char *user_name);
void				nm_dialup_manager_remove_connection			(NMDialupManager *manager, NMDialupConnection *dialup);
char	**			nm_dialup_manager_get_connection_names		(NMDialupManager *manager);
void				nm_dialup_manager_dispose					(NMDialupManager *manager);

NMDialupActRequest *	nm_dialup_manager_get_dialup_act_request			(NMDialupManager *manager);

GSList *			nm_dialup_manager_dialup_connection_list_copy		(NMDialupManager *manager);

void				nm_dialup_manager_activate_dialup_connection		(NMDialupManager *manager, NMDialupConnection *dialup, char **password_items,
										int password_count, char **data_items, int data_count );
void				nm_dialup_manager_deactivate_dialup_connection	(NMDialupManager *manager);

NMDialupConnection *	nm_dialup_manager_find_connection_by_name		(NMDialupManager *manager, const char *con_name);
NMDialupService *		nm_dialup_manager_find_service_by_name		(NMDialupManager *manager, const char *service_name);

gboolean			nm_dialup_manager_process_signal				(NMDialupManager *manager, DBusMessage *signal);
gboolean			nm_dialup_manager_process_name_owner_changed	(NMDialupManager *manager, const char *service, const char *old_owner, const char *new_owner);

void				nm_dialup_manager_schedule_dialup_activation_failed(NMDialupManager *manager, NMDialupActRequest *req);
void				nm_dialup_manager_schedule_dialup_connection_died	(NMDialupManager *manager, NMDialupActRequest *req);

gboolean			nm_dialup_manager_is_connecting			(NMDialupManager *manager);
gboolean			nm_dialup_manager_is_connected			(NMDialupManager *manager);

#endif  /* NM_DIALUP_MANAGER_H */
