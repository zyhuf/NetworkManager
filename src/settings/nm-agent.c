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
 * Copyright (C) 2010 - 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-agent.h"

G_DEFINE_INTERFACE (NMAgent, nm_agent, G_TYPE_OBJECT)

#define NM_AGENT_GET_PRIVATE(self) _NM_GET_PRIVATE (self, NMAgent, NM_IS_AGENT)

const char *
nm_agent_get_description (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, "");

	return NM_AGENT_GET_INTERFACE (agent)->get_description (agent);
}

const char *
nm_agent_get_dbus_owner (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, "");

	return NM_AGENT_GET_INTERFACE (agent)->get_dbus_owner (agent);
}

const char *
nm_agent_get_identifier (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, "");

	return NM_AGENT_GET_INTERFACE (agent)->get_identifier (agent);
}

gulong
nm_agent_get_owner_uid  (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, 0);

	return NM_AGENT_GET_INTERFACE (agent)->get_owner_uid  (agent);
}

const char *
nm_agent_get_owner_username (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, "");

	return NM_AGENT_GET_INTERFACE (agent)->get_owner_username (agent);
}

gulong
nm_agent_get_pid (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, 0);

	return NM_AGENT_GET_INTERFACE (agent)->get_pid (agent);
}

NMSecretAgentCapabilities
nm_agent_get_capabilities (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, NM_SECRET_AGENT_CAPABILITY_NONE);

	return NM_AGENT_GET_INTERFACE (agent)->get_capabilities (agent);
}

NMAuthSubject *
nm_agent_get_subject (NMAgent *agent)
{
	g_return_val_if_fail (agent != NULL, NULL);

	return NM_AGENT_GET_INTERFACE (agent)->get_subject (agent);
}

/**
 * nm_agent_add_permission:
 * @agent: A #NMAgent.
 * @permission: The name of the permission
 *
 * Records whether or not the agent has a given permission.
 */
void
nm_agent_add_permission (NMAgent *agent,
                         const char *permission,
                         gboolean allowed)
{
	NMAgentInterface *iface;

	g_return_if_fail (agent != NULL);
	iface = NM_AGENT_GET_INTERFACE (agent);

	if (iface->add_permission)
		iface->add_permission (agent, permission, allowed);
}

/**
 * nm_agent_has_permission:
 * @agent: A #NMAgent.
 * @permission: The name of the permission to check for
 *
 * Returns whether or not the agent has the given permission.
 *
 * Returns: %TRUE if the agent has the given permission, %FALSE if it does not
 * or if the permission was not previous recorded with
 * nm_agent_add_permission().
 */
gboolean
nm_agent_has_permission (NMAgent *agent, const char *permission)
{
	NMAgentInterface *iface;

	g_return_val_if_fail (agent != NULL, FALSE);
	iface = NM_AGENT_GET_INTERFACE (agent);

	if (iface->has_permission)
		return iface->has_permission (agent, permission);
	else
		return FALSE;
}

NMAgentCallId
nm_agent_get_secrets (NMAgent *agent,
                             const char *path,
                             NMConnection *connection,
                             const char *setting_name,
                             const char **hints,
                             NMSecretAgentGetSecretsFlags flags,
                             NMAgentCallback callback,
                             gpointer callback_data)
{
	NMAgentInterface *iface;

	g_return_val_if_fail (agent != NULL, FALSE);
	iface = NM_AGENT_GET_INTERFACE (agent);

	if (iface->get_secrets)
		return iface->get_secrets (agent, path, connection, setting_name, hints, flags, callback, callback_data);
	else
		return NULL;
}

/**
 * nm_agent_cancel_secrets:
 * @agent: #NMAgent instance
 * @call_id: the call id to cancel
 *
 * It is an error to pass an invalid @call_id or a @call_id for an operation
 * that already completed. NMAgent will always invoke the callback,
 * also for cancel() and dispose().
 * In case of nm_agent_cancel_secrets() this will synchronously invoke the
 * callback before nm_agent_cancel_secrets() returns.
 */
void
nm_agent_cancel_secrets (NMAgent *agent, NMAgentCallId call_id)
{
	g_return_if_fail (agent != NULL);

	return NM_AGENT_GET_INTERFACE (agent)->cancel_secrets (agent, call_id);
}

NMAgentCallId
nm_agent_save_secrets (NMAgent *agent,
                              const char *path,
                              NMConnection *connection,
                              NMAgentCallback callback,
                              gpointer callback_data)
{
	NMAgentInterface *iface;

	g_return_val_if_fail (agent != NULL, FALSE);
	iface = NM_AGENT_GET_INTERFACE (agent);

	if (iface->save_secrets)
		return iface->save_secrets (agent, path, connection, callback, callback_data);
	else
		return NULL;
}

NMAgentCallId
nm_agent_delete_secrets (NMAgent *agent,
                                const char *path,
                                NMConnection *connection,
                                NMAgentCallback callback,
                                gpointer callback_data)
{
	NMAgentInterface *iface;

	g_return_val_if_fail (agent != NULL, NULL);
	iface = NM_AGENT_GET_INTERFACE (agent);

	if (iface->delete_secrets)
		return iface->delete_secrets (agent, path, connection, callback, callback_data);
	else
		return NULL;
}

static void
nm_agent_default_init (NMAgentInterface *config_class)
{
	g_signal_new (NM_AGENT_DISCONNECTED,
	                  NM_TYPE_AGENT,
	                  G_SIGNAL_RUN_FIRST,
	                  0,
	                  NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);
}
