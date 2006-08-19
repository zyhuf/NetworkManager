/* NetworkManager -- Network link manager
 *
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
 */


#include <glib.h>
#include <string.h>
#include "nm-dialup-act-request.h"
#include "nm-dbus-dialup.h"


struct NMDialupActRequest
{
  guint			refcount;
  NMDialupActStage	stage;
  
  NMDialupManager *	manager;
  NMDialupService *	service;
  NMDialupConnection *	dialup;

  char **		password_items;
  int			password_count;
  char **		data_items;
  int			data_count;

  guint			daemon_wait_count;
  guint			callback_id;
  gboolean		canceled;
};


NMDialupActRequest *nm_dialup_act_request_new (NMDialupManager *manager, NMDialupService *service, NMDialupConnection *dialup,
								 char **password_items, int password_count,
								  char **data_items, int data_count)
{
	NMDialupActRequest	*req;

	g_return_val_if_fail (manager != NULL, NULL);
	g_return_val_if_fail (service != NULL, NULL);
	g_return_val_if_fail (dialup != NULL, NULL);
	g_return_val_if_fail (password_items != NULL, NULL);
	g_return_val_if_fail (data_items != NULL, NULL);

	req = g_malloc0 (sizeof (NMDialupActRequest));
	req->refcount = 1;
	req->stage = NM_DIALUP_ACT_STAGE_PREPARE;

	req->manager = manager;
	nm_dialup_service_ref (service);
	req->service = service;
	nm_dialup_connection_ref (dialup);
	req->dialup = dialup;

	req->password_items = g_strdupv (password_items);
	req->password_count = password_count;
	req->data_items = g_strdupv (data_items);
	req->data_count = data_count;

	return req;
}


void nm_dialup_act_request_ref (NMDialupActRequest *req)
{
	g_return_if_fail (req != NULL);

	req->refcount++;
}


void nm_dialup_act_request_unref (NMDialupActRequest *req)
{
	g_return_if_fail (req != NULL);

	req->refcount--;
	if (req->refcount == 0)
	{
		nm_dialup_service_unref (req->service);
		nm_dialup_connection_unref (req->dialup);

		g_strfreev (req->password_items);
		g_strfreev (req->data_items);

		memset (req, 0, sizeof (NMDialupActRequest));
		g_free (req);
	}
}

gboolean nm_dialup_act_request_is_activating (NMDialupActRequest *req)
{
	gboolean	activating = FALSE;

	g_return_val_if_fail (req != NULL, FALSE);

	switch (req->stage)
	{
		case NM_DIALUP_ACT_STAGE_PREPARE:
		case NM_DIALUP_ACT_STAGE_CONNECT:
		case NM_DIALUP_ACT_STAGE_IP_CONFIG_GET:
			activating = TRUE;
			break;

		default:
			break;			
	}

	return activating;
}

gboolean nm_dialup_act_request_is_activated (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, FALSE);
	
	return (req->stage == NM_DIALUP_ACT_STAGE_ACTIVATED) ? TRUE : FALSE;
}

gboolean nm_dialup_act_request_is_failed (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, FALSE);
	
	return (req->stage == NM_DIALUP_ACT_STAGE_FAILED) ? TRUE : FALSE;
}

NMDialupManager *nm_dialup_act_request_get_manager (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, NULL);

	return req->manager;
}

NMDialupService * nm_dialup_act_request_get_service (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, NULL);

	return req->service;
}


NMDialupConnection * nm_dialup_act_request_get_connection (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, NULL);

	return req->dialup;
}

const char ** nm_dialup_act_request_get_password_items (NMDialupActRequest *req, guint *count)
{
	g_return_val_if_fail (req != NULL, NULL);
	g_return_val_if_fail (count != NULL, NULL);

	*count = req->password_count;
	return (const char **) (req->password_items);
}

const char ** nm_dialup_act_request_get_data_items (NMDialupActRequest *req, guint *count)
{
	g_return_val_if_fail (req != NULL, NULL);
	g_return_val_if_fail (count != NULL, NULL);

	*count = req->data_count;
	return (const char **) (req->data_items);
}

void nm_dialup_act_request_cancel (NMDialupActRequest *req)
{
	g_return_if_fail (req != NULL);

	req->canceled = TRUE;
}

gboolean nm_dialup_act_request_should_cancel (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, FALSE);

	return req->canceled;
}

NMDialupActStage nm_dialup_act_request_get_stage (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, NM_DIALUP_ACT_STAGE_UNKNOWN);

	return req->stage;
}

void nm_dialup_act_request_set_stage (NMDialupActRequest *req, NMDialupActStage stage)
{
	NMDialupActStage	old_stage;

	g_return_if_fail (req != NULL);

	old_stage = req->stage;
	if (old_stage != stage)
	{
		DBusConnection *dbus_connection = nm_dialup_service_get_dbus_connection (req->service);

		req->stage = stage;
		nm_dbus_dialup_signal_dialup_connection_state_change (dbus_connection, req->dialup, req->stage);
	}
}

guint nm_dialup_act_request_get_daemon_wait_count (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, 0);

	return req->daemon_wait_count;
}

void nm_dialup_act_request_set_daemon_wait_count (NMDialupActRequest *req, guint count)
{
	g_return_if_fail (req != NULL);

	req->daemon_wait_count = count;
}

guint nm_dialup_act_request_get_callback_id (NMDialupActRequest *req)
{
	g_return_val_if_fail (req != NULL, 0);

	return req->callback_id;
}

void nm_dialup_act_request_set_callback_id (NMDialupActRequest *req, guint id)
{
	g_return_if_fail (req != NULL);

	req->callback_id = id;
}

