/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * (C) Copyright 2004-2005 Red Hat, Inc.
 * (C) Copyright 2006      Tim Niemueller
 */

#ifndef APPLET_DBUS_DIALUP_H
#define APPLET_DBUS_DIALUP_H

#include <glib.h>
#include <dbus/dbus.h>
#include "dialup-connection.h"

void		nma_dbus_dialup_set_last_attempt_status				(NMApplet *applet, const char *dialup_name, gboolean last_attempt_success);

void		nma_dbus_dialup_update_one_dialup_connection			(NMApplet *applet, const char *dialup_name);
void		nma_dbus_dialup_update_dialup_connections			(NMApplet *applet);
void		nma_dbus_dialup_remove_one_dialup_connection			(NMApplet *applet, const char *dialup_name);

void		nma_dbus_dialup_activate_connection				(DBusConnection *connection, const char *name, GSList *user_pass);
void		nma_dbus_dialup_deactivate_connection				(DBusConnection *connection);
void		nma_dbus_dialup_update_dialup_connection_stage		(NMApplet *applet, const char *dialup_name, NMDialupActStage dialup_state);

#endif
