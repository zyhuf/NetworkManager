/* NetworkManager -- Network link manager
 *
 * Tim Niemueller [www.niemueller.de]
 * based on work by Dan Williams <dcbw@redhat.com>
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

#ifndef NM_DIALUP_SERVICE_H
#define NM_DIALUP_SERVICE_H


#include <dbus/dbus.h>
#include "NetworkManager.h"
#include "NetworkManagerDialup.h"
#include "NetworkManagerMain.h"
#include "nm-dialup-connection.h"

typedef struct NMDialupService NMDialupService;


NMDialupService *	nm_dialup_service_new				(NMDialupManager *manager, NMData *app_data);

void			nm_dialup_service_ref				(NMDialupService *service);
void			nm_dialup_service_unref			(NMDialupService *service);

const char *	nm_dialup_service_get_name			(NMDialupService *service);
void			nm_dialup_service_set_name			(NMDialupService *service, const char *name);

const char *	nm_dialup_service_get_service_name	(NMDialupService *service);
void			nm_dialup_service_set_service_name	(NMDialupService *service, const char *name);

const char *	nm_dialup_service_get_program		(NMDialupService *service);
void			nm_dialup_service_set_program		(NMDialupService *service, const char *program);

DBusConnection* nm_dialup_service_get_dbus_connection (NMDialupService *service);

NMDialupState	nm_dialup_service_get_state			(NMDialupService *service);

gboolean		nm_dialup_service_name_owner_changed	(NMDialupService *service, NMDialupActRequest *req, const char *old, const char *new);
gboolean		nm_dialup_service_process_signal		(NMDialupService *service, NMDialupActRequest *req, DBusMessage *message);

void			nm_dialup_service_start_connection	(NMDialupService *service, NMDialupActRequest *req);
void			nm_dialup_service_stop_connection	(NMDialupService *service, NMDialupActRequest *req);

#endif
