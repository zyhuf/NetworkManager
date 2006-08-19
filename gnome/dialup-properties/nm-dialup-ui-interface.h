/***************************************************************************
 * CVSID: $Id$
 *
 * nm-dialup-ui-interface.h : Public interface for dialup UI editing widgets
 *
 * Copyright (C) 2005 David Zeuthen, <davidz@redhat.com>
 * Copyright (C) 2006 Tim Niemueller [www.niemueller.de]
 *
 * === 
 * NOTE NOTE NOTE: All source for nm-dialup-properties is licensed to you
 * under your choice of the Academic Free License version 2.0, or the
 * GNU General Public License version 2.
 * ===
 *
 * Licensed under the Academic Free License version 2.0
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **************************************************************************/

#ifndef NM_DIALUP_UI_INTERFACE_H
#define NM_DIALUP_UI_INTERFACE_H

#ifndef NM_DIALUP_API_SUBJECT_TO_CHANGE
#error "Please define NM_DIALUP_API_SUBJECT_TO_CHANGE to acknowledge your understanding that NetworkManager hasn't reached 1.0 and is subject to protocol and API churn. See the README for a full explanation."
#endif

#include <gtk/gtk.h>

struct _NetworkManagerDialupUI;
typedef struct _NetworkManagerDialupUI NetworkManagerDialupUI;

typedef void (*NetworkManagerDialupUIDialogValidityCallback) (NetworkManagerDialupUI *self,
							   gboolean is_valid, 
							   gpointer user_data);


struct _NetworkManagerDialupUI {
	const char *(*get_display_name) (NetworkManagerDialupUI *self);

	const char *(*get_service_name) (NetworkManagerDialupUI *self);
	const char *(*get_service_type) (NetworkManagerDialupUI *self);

	GtkWidget *(*get_widget) (NetworkManagerDialupUI *self, GSList *properties, const char *connection_name);

	void (*set_validity_changed_callback) (NetworkManagerDialupUI *self, 
					       NetworkManagerDialupUIDialogValidityCallback cb,
					       gpointer user_data);

	gboolean (*is_valid) (NetworkManagerDialupUI *self);

	/*
	 * get_confirmation_details:
	 * retval is allocated and must be freed
	 */
	void (*get_confirmation_details)(NetworkManagerDialupUI *self, gchar **retval);

	char *(*get_connection_name) (NetworkManagerDialupUI *self);

	GSList *(*get_properties) (NetworkManagerDialupUI *self);

	gboolean (*can_export) (NetworkManagerDialupUI *self);

	gboolean (*import_file) (NetworkManagerDialupUI *self, const char *path);

	gboolean (*export) (NetworkManagerDialupUI *self, GSList *properties, const char *connection_name);

	gpointer data;
};

#endif /* NM_DIALUP_UI_INTERFACE_H */

