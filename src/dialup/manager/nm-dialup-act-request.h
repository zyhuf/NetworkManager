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

#ifndef NM_DIALUP_ACT_REQUEST_H
#define NM_DIALUP_ACT_REQUEST_H

#include <glib.h>
#include "NetworkManager.h"
#include "NetworkManagerMain.h"
#include "nm-dialup-connection.h"
#include "nm-dialup-service.h"

NMDialupActRequest *	nm_dialup_act_request_new			(NMDialupManager *manager, NMDialupService *service, NMDialupConnection *dialup,
									 char **password_items, int password_count, char **data_items, int data_count);
void				nm_dialup_act_request_ref			(NMDialupActRequest *req);
void				nm_dialup_act_request_unref			(NMDialupActRequest *req);

gboolean			nm_dialup_act_request_is_activating	(NMDialupActRequest *req);
gboolean			nm_dialup_act_request_is_activated	(NMDialupActRequest *req);
gboolean			nm_dialup_act_request_is_failed		(NMDialupActRequest *req);

NMDialupManager *		nm_dialup_act_request_get_manager	(NMDialupActRequest *req);
NMDialupService *		nm_dialup_act_request_get_service	(NMDialupActRequest *req);
NMDialupConnection *	nm_dialup_act_request_get_connection	(NMDialupActRequest *req);

const char **		nm_dialup_act_request_get_password_items	(NMDialupActRequest *req, guint *count);
const char **		nm_dialup_act_request_get_data_items	(NMDialupActRequest *req, guint *count);

void				nm_dialup_act_request_cancel		(NMDialupActRequest *req);
gboolean			nm_dialup_act_request_should_cancel	(NMDialupActRequest *req);

NMDialupActStage		nm_dialup_act_request_get_stage		(NMDialupActRequest *req);
void				nm_dialup_act_request_set_stage		(NMDialupActRequest *req, NMDialupActStage stage);

guint			nm_dialup_act_request_get_daemon_wait_count	(NMDialupActRequest *req);
void				nm_dialup_act_request_set_daemon_wait_count	(NMDialupActRequest *req, guint count);

guint			nm_dialup_act_request_get_callback_id	(NMDialupActRequest *req);
void				nm_dialup_act_request_set_callback_id	(NMDialupActRequest *req, guint timeout);

#endif
