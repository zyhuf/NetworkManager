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

#ifndef __NETWORKMANAGER_AGENT_H__
#define __NETWORKMANAGER_AGENT_H__

#include "nm-connection.h"

#define NM_TYPE_AGENT            (nm_agent_get_type ())
#define NM_AGENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_AGENT, NMAgent))
#define NM_IS_AGENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_AGENT))
#define NM_AGENT_GET_INTERFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), NM_TYPE_AGENT, NMAgentInterface))

#define NM_AGENT_DISCONNECTED "disconnected"

typedef struct _NMAgent NMAgent;

typedef struct _NMAgentCallId *NMAgentCallId;

typedef void (*NMAgentCallback) (NMAgent *agent,
                                 NMAgentCallId call_id,
                                 GVariant *new_secrets, /* NULL for save & delete */
                                 GError *error,
                                 gpointer user_data);

typedef struct {
        GTypeInterface g_iface;

	NMAgent (**nm_agent_new) (GDBusMethodInvocation *context,
                                  NMAuthSubject *subject,
                                  const char *identifier,
                                  NMSecretAgentCapabilities capabilities);

	const char *(*get_description) (NMAgent *agent);

	const char *(*get_dbus_owner) (NMAgent *agent);

	const char *(*get_identifier) (NMAgent *agent);

	gulong      (*get_owner_uid)  (NMAgent *agent);

	const char *(*get_owner_username) (NMAgent *agent);

	gulong      (*get_pid)        (NMAgent *agent);

	NMSecretAgentCapabilities (*get_capabilities) (NMAgent *agent);

	NMAuthSubject *(*get_subject) (NMAgent *agent);

	void        (*add_permission) (NMAgent *agent,
                                       const char *permission,
                                       gboolean allowed);

	gboolean    (*has_permission) (NMAgent *agent,
                                       const char *permission);

	NMAgentCallId (*get_secrets)  (NMAgent *agent,
                                       const char *path,
                                       NMConnection *connection,
                                       const char *setting_name,
                                       const char **hints,
                                       NMSecretAgentGetSecretsFlags flags,
                                       NMAgentCallback callback,
                                       gpointer callback_data);

	void        (*cancel_secrets) (NMAgent *agent,
                                 NMAgentCallId call_id);

	NMAgentCallId (*save_secrets) (NMAgent *agent,
                                       const char *path,
                                       NMConnection *connection,
                                       NMAgentCallback callback,
                                       gpointer callback_data);

	NMAgentCallId (*delete_secrets) (NMAgent *agent,
                                         const char *path,
                                         NMConnection *connection,
                                         NMAgentCallback callback,
                                         gpointer callback_data);
} NMAgentInterface;


GType nm_agent_get_type (void);

NMAgent *nm_agent_new (GDBusMethodInvocation *context,
                       NMAuthSubject *subject,
                       const char *identifier,
                       NMSecretAgentCapabilities capabilities);

const char *nm_agent_get_description (NMAgent *agent);

const char *nm_agent_get_dbus_owner (NMAgent *agent);

const char *nm_agent_get_identifier (NMAgent *agent);

gulong      nm_agent_get_owner_uid  (NMAgent *agent);

const char *nm_agent_get_owner_username (NMAgent *agent);

gulong      nm_agent_get_pid        (NMAgent *agent);

NMSecretAgentCapabilities nm_agent_get_capabilities (NMAgent *agent);

NMAuthSubject *nm_agent_get_subject (NMAgent *agent);

void        nm_agent_add_permission (NMAgent *agent,
                                     const char *permission,
                                     gboolean allowed);

gboolean    nm_agent_has_permission (NMAgent *agent,
                                     const char *permission);

NMAgentCallId nm_agent_get_secrets  (NMAgent *agent,
                                     const char *path,
                                     NMConnection *connection,
                                     const char *setting_name,
                                     const char **hints,
                                     NMSecretAgentGetSecretsFlags flags,
                                     NMAgentCallback callback,
                                     gpointer callback_data);

void        nm_agent_cancel_secrets (NMAgent *agent,
                                     NMAgentCallId call_id);

NMAgentCallId nm_agent_save_secrets (NMAgent *agent,
                                     const char *path,
                                     NMConnection *connection,
                                     NMAgentCallback callback,
                                     gpointer callback_data);

NMAgentCallId nm_agent_delete_secrets (NMAgent *agent,
                                       const char *path,
                                       NMConnection *connection,
                                       NMAgentCallback callback,
                                       gpointer callback_data);

#endif /* __NETWORKMANAGER_AGENT_H__ */
