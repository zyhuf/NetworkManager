/* NetworkManager -- dialup connection
 *
 * Copyright (C) 2006 by Tim Niemueller [www.niemueller.de]
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
 * (C) Copyright 2004 Red Hat, Inc.
 * (C) Copyright 2006 Tim Niemueller
 */

#ifndef DIALUP_CONNECTION_H
#define DIALUP_CONNECTION_H

#include "applet.h"
#include "NetworkManagerDialup.h"

DialupConnection *	nma_dialup_connection_new			(const char *name);
DialupConnection *	nma_dialup_connection_copy			(DialupConnection *dialup);
void			nma_dialup_connection_ref			(DialupConnection *dialup);
void			nma_dialup_connection_unref		(DialupConnection *dialup);

const char *		nma_dialup_connection_get_name		(DialupConnection *dialup);

const char *		nma_dialup_connection_get_service	(DialupConnection *dialup);
void			nma_dialup_connection_set_service	(DialupConnection *dialup, const char *service);

NMDialupActStage	nma_dialup_connection_get_stage		(DialupConnection *dialup);
void			nma_dialup_connection_set_stage		(DialupConnection *dialup, NMDialupActStage stage);

gboolean		nma_dialup_connection_is_activating	(DialupConnection *dialup);

DialupConnection *	nma_dialup_connection_find_by_name	(GSList *list, const char *name);

#endif
