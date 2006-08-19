/* NetworkManager -- dialup connection
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
 * (C) Copyright 2004 Red Hat, Inc.
 * (C) Copyright 2006 Tim Niemueller
 */

#include <string.h>
#include "dialup-connection.h"


struct DialupConnection
{
  int			refcount;
  char	*		name;
  char *		service;
  NMDialupActStage	stage;
};


DialupConnection *
nma_dialup_connection_new (const char *name)
{
	DialupConnection *dialup;

	g_return_val_if_fail (name != NULL, NULL);

	dialup = g_malloc0 (sizeof (DialupConnection));
	dialup->refcount = 1;
	dialup->name = g_strdup (name);

	return dialup;
}


DialupConnection *
nma_dialup_connection_copy (DialupConnection *src_dialup)
{
	DialupConnection *dst_dialup;

	g_return_val_if_fail (src_dialup != NULL, NULL);

	dst_dialup = g_malloc0 (sizeof (DialupConnection));
	dst_dialup->refcount = 1;
	dst_dialup->name = g_strdup (src_dialup->name);
	dst_dialup->service = src_dialup->service ? g_strdup (src_dialup->service) : NULL;
	dst_dialup->stage = src_dialup->stage;

	return dst_dialup;
}


void
nma_dialup_connection_ref (DialupConnection *dialup)
{
	g_return_if_fail (dialup != NULL);

	dialup->refcount++;
}


void
nma_dialup_connection_unref (DialupConnection *dialup)
{
	g_return_if_fail (dialup != NULL);

	dialup->refcount--;
	if (dialup->refcount <= 0)
	{
		g_free (dialup->name);
		g_free (dialup->service);
		memset (dialup, 0, sizeof (DialupConnection));
		g_free (dialup);
	}
}


const char *
nma_dialup_connection_get_name (DialupConnection *dialup)
{
	g_return_val_if_fail (dialup != NULL, NULL);

	return dialup->name;
}

const char *
nma_dialup_connection_get_service (DialupConnection *dialup)
{
	g_return_val_if_fail (dialup != NULL, NULL);

	return dialup->service;
}


void
nma_dialup_connection_set_service (DialupConnection *dialup, const char *service)
{
	g_return_if_fail (dialup != NULL);
	g_return_if_fail (service != NULL);

	g_free (dialup->service);
	dialup->service = g_strdup (service);
}



static int
is_same_name (DialupConnection *dialup, const char *name)
{
	if (!dialup || !name || !nma_dialup_connection_get_name (dialup))
		return -1;

	return strcmp (nma_dialup_connection_get_name (dialup), name);
}


DialupConnection *
nma_dialup_connection_find_by_name (GSList *list, const char *name)
{
	GSList		*elt;
	DialupConnection	*dialup = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	if (!list)
		return NULL;

	if ((elt = g_slist_find_custom (list, name, (GCompareFunc) is_same_name)))
		dialup = elt->data;

	return dialup;	
}

NMDialupActStage
nma_dialup_connection_get_stage (DialupConnection *dialup)
{
	g_return_val_if_fail (dialup != NULL, NM_DIALUP_ACT_STAGE_UNKNOWN);

	return dialup->stage;
}


void
nma_dialup_connection_set_stage (DialupConnection *dialup, NMDialupActStage stage)
{
	g_return_if_fail (dialup != NULL);

	dialup->stage = stage;
}


gboolean
nma_dialup_connection_is_activating (DialupConnection *dialup)
{
	NMDialupActStage stage;

	g_return_val_if_fail (dialup != NULL, FALSE);

	stage = nma_dialup_connection_get_stage (dialup);
	if (stage == NM_DIALUP_ACT_STAGE_PREPARE ||
		stage == NM_DIALUP_ACT_STAGE_CONNECT ||
		stage == NM_DIALUP_ACT_STAGE_IP_CONFIG_GET)
		return TRUE;

	return FALSE;
}
