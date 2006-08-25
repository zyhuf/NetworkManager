/* nm-pppoe-properties.c : GNOME UI dialogs for configuring PPPoE connections
 *
 * Copyright (C) 2005-2006 Tim Niemueller <tim@niemueller.de>
 * Based on work by David Zeuthen, <davidz@redhat.com>
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
 * $Id$
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <string.h>
#include <stdlib.h>
#include <glade/glade.h>

#include "NetworkManager.h"

#define NM_DIALUP_API_SUBJECT_TO_CHANGE

#include "nm-dialup-ui-interface.h"

typedef struct _NetworkManagerDialupUIImpl NetworkManagerDialupUIImpl;

struct _NetworkManagerDialupUIImpl {
  NetworkManagerDialupUI parent;

  NetworkManagerDialupUIDialogValidityCallback callback;
  gpointer           callback_user_data;

  DBusConnection    *dbus;

  GladeXML          *xml;
  GtkWidget         *widget;
  GtkEntry          *w_connection_name;
  GtkComboBoxEntry  *w_interface;
  GtkEntry          *w_service_name;
  GtkEntry          *w_ac_name;
  GtkCheckButton    *w_synchronous;
  GtkExpander       *w_opt_info_expander;
  GtkButton         *w_import_button;

  GSList            *interfaces;
};

static void
pppoe_clear_widget (NetworkManagerDialupUIImpl *impl)
{
  gtk_entry_set_text (impl->w_connection_name, "");
  gtk_combo_box_set_active (GTK_COMBO_BOX (impl->w_interface), -1);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (impl->w_synchronous), FALSE);
  gtk_entry_set_text (impl->w_service_name,   "");
  gtk_entry_set_text (impl->w_ac_name,   "");
  gtk_expander_set_expanded (impl->w_opt_info_expander, FALSE);
}

static const char *
impl_get_display_name (NetworkManagerDialupUI *self)
{
  return _("DSL (PPP over Ethernet, PPPoE)");
}

static const char *
impl_get_service_name (NetworkManagerDialupUI *self)
{
  return "org.freedesktop.NetworkManager.ppp";
}

static const char *
impl_get_service_type (NetworkManagerDialupUI *self)
{
  return "pppoe";
}


static inline gboolean
message_is_error (DBusMessage *msg)
{
  g_return_val_if_fail (msg != NULL, FALSE);

  return (dbus_message_get_type (msg) == DBUS_MESSAGE_TYPE_ERROR);
}


static char *
pppoe_get_device_name(NetworkManagerDialupUI *self, char *net_path)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;
  DBusMessage *message;

  if ((message = dbus_message_new_method_call (NM_DBUS_SERVICE, net_path,
					       NM_DBUS_INTERFACE_DEVICES, "getProperties"))) {
    DBusMessage  *reply;

    reply = dbus_connection_send_with_reply_and_block (impl->dbus, message, -1, NULL);
    dbus_message_unref (message);

    if (reply) {
      char *			op = NULL;
      const char *		iface = NULL;
      dbus_uint32_t		type = 0;
      const char *		udi = NULL;
      dbus_bool_t		active = FALSE;
      const char *		ip4_address = NULL;
      const char *		broadcast = NULL;
      const char *		subnetmask = NULL;
      const char *		hw_addr = NULL;
      const char *		route = NULL;
      const char *		primary_dns = NULL;
      const char *		secondary_dns = NULL;
      dbus_int32_t		mode = -1;
      dbus_int32_t		strength = -1;
      dbus_int32_t		speed = 0;
      char *			active_network_path = NULL;
      dbus_bool_t		link_active = FALSE;
      dbus_uint32_t		caps = NM_DEVICE_CAP_NONE;
      dbus_uint32_t		type_caps = NM_DEVICE_CAP_NONE;
      char **			networks = NULL;
      int				num_networks = 0;
      NMActStage		act_stage = NM_ACT_STAGE_UNKNOWN;

      if (dbus_message_is_error (reply, NM_DBUS_NO_NETWORKS_ERROR)) {
	fprintf (stderr, "No networks available");
	dbus_message_unref (reply);
      } else if (message_is_error (reply)) {
	DBusError err;
	
	dbus_error_init (&err);
	dbus_set_error_from_message (&err, reply);
	fprintf (stderr, "dbus returned an error.\n  (%s) %s\n", err.name, err.message);
	dbus_error_free (&err);
	dbus_message_unref (reply);
      } else if (dbus_message_get_args (reply, NULL,
					DBUS_TYPE_OBJECT_PATH, &op,
					DBUS_TYPE_STRING, &iface,
					DBUS_TYPE_UINT32, &type,
					DBUS_TYPE_STRING, &udi,
					DBUS_TYPE_BOOLEAN,&active,
					DBUS_TYPE_UINT32, &act_stage,
					DBUS_TYPE_STRING, &ip4_address,
					DBUS_TYPE_STRING, &subnetmask,
					DBUS_TYPE_STRING, &broadcast,
					DBUS_TYPE_STRING, &hw_addr,
					DBUS_TYPE_STRING, &route,
					DBUS_TYPE_STRING, &primary_dns,
					DBUS_TYPE_STRING, &secondary_dns,
					DBUS_TYPE_INT32,  &mode,
					DBUS_TYPE_INT32,  &strength,
					DBUS_TYPE_BOOLEAN,&link_active,
					DBUS_TYPE_INT32,  &speed,
					DBUS_TYPE_UINT32, &caps,
					DBUS_TYPE_UINT32, &type_caps,
					DBUS_TYPE_STRING, &active_network_path,
					DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &networks, &num_networks,
					DBUS_TYPE_INVALID)) {
	if ( type == DEVICE_TYPE_802_3_ETHERNET ) {
	  // only interesting to us if it is a wired net (for PPPoe)
	  return g_strdup(iface);
	} else {
	  fprintf(stderr, "Device %s is not of type wired ethernet, ignoring\n", iface);
	}
      }
    }
  }

  return NULL;
}


static void
pppoe_load_interface_list(NetworkManagerDialupUI *self)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;
  GtkListStore *store = NULL;
  GtkTreeIter iter;

  if ( impl->interfaces != NULL ) {
    g_slist_foreach (impl->interfaces, (GFunc)g_free, NULL);
    g_slist_free (impl->interfaces);
    impl->interfaces = NULL;
  }


  store = gtk_list_store_new(1, G_TYPE_STRING);

  if ( impl->dbus != NULL ) {
    DBusMessage *message;
    printf("Preparing message\n");
    if ((message = dbus_message_new_method_call (NM_DBUS_SERVICE,
						 NM_DBUS_PATH,
						 NM_DBUS_INTERFACE,
						 "getDevices"))) {
      DBusMessage *reply;

      reply = dbus_connection_send_with_reply_and_block(impl->dbus, message, -1, NULL);
      dbus_message_unref(message);
      if (! reply) {
	fprintf(stderr, "Could not fetch devices\n");
      } else {
	if (dbus_message_is_error (reply, NM_DBUS_NO_DEVICES_ERROR)) {
	  fprintf (stderr, "No devices reported by NM");
	  dbus_message_unref (reply);
	} else if (message_is_error (reply)) {
	  DBusError err;
	    
	  dbus_error_init (&err);
	  dbus_set_error_from_message (&err, reply);
	  fprintf (stderr, "dbus returned an error.\n  (%s) %s\n", err.name, err.message);
	  dbus_error_free (&err);
	  dbus_message_unref (reply);
	} else {
	  char **devices;
	  int num_devices;
	  printf("Fetched devices\n");
	  if (dbus_message_get_args (reply, NULL,
				     DBUS_TYPE_ARRAY, DBUS_TYPE_OBJECT_PATH,
				     &devices, &num_devices, DBUS_TYPE_INVALID)) {
	    char ** item;
		
	    /* For each device, fire off a "getProperties" call */
	    for (item = devices; *item; item++)	{
	      char *iface = pppoe_get_device_name(self, *item);
	      if ( iface == NULL ) {
		printf("Ignoring %s\n", *item);
	      } else {
		printf("Received device %s, which is iface %s\n", *item, iface);
		gtk_list_store_append (store, &iter);  /* Acquire an iterator */
		gtk_list_store_set (store, &iter, 0, iface, -1);
		impl->interfaces = g_slist_append( impl->interfaces, iface );
	      }
	    }

	    dbus_free_string_array (devices);
	  }
	  dbus_message_unref (reply);
	}
      }
    }
  }

  gtk_combo_box_set_model (GTK_COMBO_BOX(impl->w_interface), GTK_TREE_MODEL(store));
  g_object_unref (G_OBJECT (store));

  if ( g_slist_length(impl->interfaces) > 0 ) {
    gtk_combo_box_set_active (GTK_COMBO_BOX(impl->w_interface), 0);
  }

}


static GtkWidget *
impl_get_widget (NetworkManagerDialupUI *self, GSList *properties,
		 const char *connection_name)
{
  GSList *i;
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;
  gboolean should_expand = FALSE;

  pppoe_clear_widget (impl);
  pppoe_load_interface_list(self);

  if (connection_name != NULL)
    gtk_entry_set_text (impl->w_connection_name, connection_name);

  for (i = properties; i != NULL && g_slist_next (i) != NULL; i = g_slist_next (g_slist_next (i))) {
    const char *key;
    const char *value;

    key = i->data;
    value = (g_slist_next (i))->data;

    if (strcmp (key, "pppoe-interface") == 0) {
      GSList *iface = g_slist_find_custom(impl->interfaces, value, (GCompareFunc)strcmp);
      if ( iface != NULL ) {
	gtk_combo_box_set_active (GTK_COMBO_BOX(impl->w_interface), g_slist_position(impl->interfaces, iface));
      } else {
	gtk_combo_box_set_active (GTK_COMBO_BOX(impl->w_interface), -1);
	gtk_entry_set_text (GTK_ENTRY(gtk_bin_get_child(GTK_BIN(impl->w_interface))), value);
      }
    } else if (strcmp (key, "pppoe-service-name") == 0) {
      if ( strlen(value) > 0 ) {
	gtk_entry_set_text (impl->w_service_name, value);
	should_expand = TRUE;
      }

    } else if (strcmp (key, "pppoe-ac-name") == 0) {
      if ( strlen(value) > 0 ) {
	gtk_entry_set_text (impl->w_ac_name, value);
	should_expand = TRUE;
      }

    } else if (strcmp (key, "pppoe-ac-name") == 0) {
      if ( strlen(value) > 0 ) {
	gtk_entry_set_text (impl->w_ac_name, value);
	should_expand = TRUE;
      }

    } else if (strcmp (key, "pppoe-synchronous") == 0) {
      if ( strcmp(value, "yes") == 0 ) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (impl->w_synchronous), TRUE);
	should_expand = TRUE;
      }
    }
  }

  gtk_expander_set_expanded (impl->w_opt_info_expander, should_expand);
  gtk_container_resize_children (GTK_CONTAINER (impl->widget));

  return impl->widget;
}

static GSList *
impl_get_properties (NetworkManagerDialupUI *self)
{
  GSList *data;
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;
  const char *connection_name;
  const char *interface;
  const char *service_name;
  const char *ac_name;
  gboolean synchronous;

  connection_name  = gtk_entry_get_text (impl->w_connection_name);
  interface        = gtk_entry_get_text (GTK_ENTRY(gtk_bin_get_child(GTK_BIN(impl->w_interface))));
  service_name     = gtk_entry_get_text (impl->w_service_name);
  ac_name          = gtk_entry_get_text (impl->w_ac_name);
  synchronous      = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (impl->w_synchronous));

  data = NULL;

  data = g_slist_append (data, g_strdup ("wvdial"));
  data = g_slist_append (data, g_strdup ("no"));

  data = g_slist_append (data, g_strdup ("pppoe"));
  data = g_slist_append (data, g_strdup ("yes"));

  data = g_slist_append (data, g_strdup ("pppoe-interface"));
  data = g_slist_append (data, g_strdup (interface));

  data = g_slist_append (data, g_strdup ("pppoe-service-name"));
  data = g_slist_append (data, g_strdup (service_name));

  data = g_slist_append (data, g_strdup ("pppoe-ac-name"));
  data = g_slist_append (data, g_strdup (ac_name));

  data = g_slist_append (data, g_strdup ("pppoe-mss"));
  data = g_slist_append (data, g_strdup ("1412"));

  data = g_slist_append (data, g_strdup ("mtu"));
  data = g_slist_append (data, g_strdup ("1492"));

  data = g_slist_append (data, g_strdup ("mru"));
  data = g_slist_append (data, g_strdup ("1492"));

  data = g_slist_append (data, g_strdup ("pppoe-session-traffic-timeout"));
  data = g_slist_append (data, g_strdup ("80"));

  data = g_slist_append (data, g_strdup ("asyncmap"));
  data = g_slist_append (data, g_strdup ("default"));

  data = g_slist_append (data, g_strdup ("lcp-echo-interval"));
  data = g_slist_append (data, g_strdup ("20"));

  data = g_slist_append (data, g_strdup ("lcp-echo-failure"));
  data = g_slist_append (data, g_strdup ("3"));

  data = g_slist_append (data, g_strdup ("pppoe-synchronous"));
  data = g_slist_append (data, g_strdup (synchronous ? "yes" : "no"));

  return data;
}

static char *
impl_get_connection_name (NetworkManagerDialupUI *self)
{
  const char *name;
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;

  name = gtk_entry_get_text (impl->w_connection_name);
  if (name != NULL)
    return g_strdup (name);
  else
    return NULL;
}


static gboolean
impl_is_valid (NetworkManagerDialupUI *self)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;
  gboolean is_valid;
  const char *connection_name;
  const char *interface;

  connection_name  = gtk_entry_get_text (impl->w_connection_name);
  interface        = gtk_entry_get_text (GTK_ENTRY(gtk_bin_get_child(GTK_BIN(impl->w_interface))));

  is_valid = TRUE;

  if ( (strlen (connection_name) == 0) ||
       (strlen (interface) == 0) ||
       (strstr (interface, " ") != NULL)  ||
       (strstr (interface, "\t") != NULL) ) {

    is_valid = FALSE;
  }

  return is_valid;
}

static void
editable_changed (GtkEditable *editable, gpointer user_data)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) user_data;

  if (impl->callback != NULL) {
    gboolean is_valid;

    is_valid = impl_is_valid (&(impl->parent));
    impl->callback (&(impl->parent), is_valid, impl->callback_user_data);
  }
}


static void
impl_set_validity_changed_callback (NetworkManagerDialupUI *self,
				    NetworkManagerDialupUIDialogValidityCallback callback,
				    gpointer user_data)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;

  impl->callback = callback;
  impl->callback_user_data = user_data;
}


static void
impl_set_dbus_connection (NetworkManagerDialupUI *self, DBusConnection *con)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;

  impl->dbus = con;
}


static void
impl_get_confirmation_details (NetworkManagerDialupUI *self, gchar **retval)
{
  GString *buf;
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;
  const char *connection_name;
  const char *interface;
  const char *service_name;
  const char *ac_name;
  gboolean synchronous;

  connection_name        = gtk_entry_get_text (impl->w_connection_name);
  interface              = gtk_entry_get_text (GTK_ENTRY(gtk_bin_get_child(GTK_BIN(impl->w_interface))));
  service_name           = gtk_entry_get_text (impl->w_service_name);
  ac_name                = gtk_entry_get_text (impl->w_ac_name);
  synchronous            = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (impl->w_synchronous));

  // This is risky, should be variable length depending on actual data!
  buf = g_string_sized_new (1024);

  g_string_append (buf, _("The following PPPoE connection will be created:"));
  g_string_append (buf, "\n\n\t");
  g_string_append_printf (buf, _("Name:  %s"), connection_name);
  g_string_append (buf, "\n\n\t");

  g_string_append_printf (buf, _("Interface:  %s"), interface);
  g_string_append (buf, "\n\t");

  g_string_append_printf (buf, _("Synchronous PPP:  %s"), (synchronous ? "Yes" : "No"));
  g_string_append (buf, "\n\t");  

  if ( (service_name != NULL) && (strlen(service_name) > 0) ) {
    g_string_append_printf( buf, _("Service Name: %s"), service_name);
    g_string_append (buf, "\n\t");
  }

  if ( (ac_name != NULL) && (strlen(ac_name) > 0) ) {
    g_string_append_printf( buf, _("Access Concentrator: %s"), ac_name);
    g_string_append (buf, "\n\t");
  }

  *retval = g_string_free (buf, FALSE);
}

static gboolean
import_from_file (NetworkManagerDialupUIImpl *impl, const char *path)
{
  char *basename;
  GKeyFile *keyfile;
  gboolean file_is_good;

  file_is_good = FALSE;
  basename = g_path_get_basename (path);

  keyfile = g_key_file_new ();
  if (g_key_file_load_from_file (keyfile, path, 0, NULL)) {
    char *connection_name = NULL;
    char *interface = NULL;
    char *service_name = NULL;
    char *ac_name = NULL;
    char *synchronous;

    connection_name = g_key_file_get_string (keyfile, "pppoe", "description", NULL);
    interface       = g_key_file_get_string (keyfile, "pppoe", "interface", NULL);
    service_name    = g_key_file_get_string (keyfile, "pppoe", "service-name", NULL);
    ac_name         = g_key_file_get_string (keyfile, "pppoe", "ac-name", NULL);
    synchronous     = g_key_file_get_string (keyfile, "pppoe", "synchronous", NULL);

    file_is_good = FALSE;

    /* sanity check data */
    if ( (connection_name != NULL) &&
	 (interface != NULL ) ) {

      // basics ok
      file_is_good = TRUE;
    }

    if (file_is_good) {
      gboolean should_expand = FALSE;
      GSList *iface = g_slist_find_custom(impl->interfaces, interface, (GCompareFunc)strcmp);
      if ( iface != NULL ) {
	gtk_combo_box_set_active (GTK_COMBO_BOX(impl->w_interface), g_slist_position(impl->interfaces, iface));
      } else {
	gtk_combo_box_set_active (GTK_COMBO_BOX(impl->w_interface), -1);
	gtk_entry_set_text (GTK_ENTRY(gtk_bin_get_child(GTK_BIN(impl->w_interface))), interface);
      }

      gtk_entry_set_text (impl->w_connection_name, connection_name);

      if ( (service_name != NULL) && (strlen(service_name) > 0) ) {
	gtk_entry_set_text (impl->w_service_name, service_name);
	should_expand = TRUE;
      }

      if ( (ac_name != NULL) && (strlen(ac_name) > 0) ) {
	gtk_entry_set_text (impl->w_ac_name, ac_name);
	should_expand = TRUE;
      }

      if ( (synchronous != NULL) && (strcmp(synchronous, "yes") == 0) ) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (impl->w_synchronous), TRUE);
	should_expand = TRUE;
      }

      gtk_expander_set_expanded (impl->w_opt_info_expander, should_expand);
    } else {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (NULL,
				       GTK_DIALOG_DESTROY_WITH_PARENT,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_CLOSE,
				       _("Cannot import settings"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						_("The PPPoE dialup settings file '%s' does not contain valid data."), basename);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }

    g_key_file_free (keyfile);

    g_free (connection_name);
    g_free (interface);
    g_free (service_name);
    g_free (ac_name);
  }

  g_free (basename);

  return file_is_good;
}

static void
import_button_clicked (GtkButton *button, gpointer user_data)
{
  char *filename = NULL;
  GtkWidget *dialog;
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) user_data;

  dialog = gtk_file_chooser_dialog_new (_("Select file to import"),
					NULL,
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					NULL);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  }

  gtk_widget_destroy (dialog);

  if (filename != NULL) {
    import_from_file (impl, filename);
    g_free (filename);
  }
}


static gboolean
impl_can_export (NetworkManagerDialupUI *self)
{
  return TRUE;
}

static gboolean
impl_import_file (NetworkManagerDialupUI *self, const char *path)
{
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;

  return import_from_file (impl, path);
}

static gboolean
export_to_file (NetworkManagerDialupUIImpl *impl, const char *path,
		GSList *properties, const char *connection_name)
{
  FILE *f;
  GSList *i;
  const char *interface = NULL;
  const char *service_name = NULL;
  const char *ac_name = NULL;
  const char *synchronous = NULL;
  gboolean ret;

  /*printf ("in export_to_file; path='%s'\n", path);*/

  for (i = properties; i != NULL && g_slist_next (i) != NULL; i = g_slist_next (g_slist_next (i))) {
    const char *k;
    const char *value;

    k = i->data;
    value = (g_slist_next (i))->data;

    if (strcmp (k, "interface") == 0) {
      interface = value;
    } else if (strcmp (k, "pppoe-service-name") == 0) {
      service_name = value;
    } else if (strcmp (k, "pppoe-ac-name") == 0) {
      ac_name = value;
    } else if (strcmp (k, "pppoe-synchronous") == 0) {
      synchronous = value;
    }
  }


  f = fopen (path, "w");
  if (f != NULL) {

    fprintf (f,
	     "[pppoe]\n"
	     "description=%s\n"
	     "interface=%s\n"
	     "synchronous=%s\n"
	     "service-name=%s\n"
	     "ac-name=%s\n",
	     /* Description */ connection_name,
	     /* Iface */       interface,
	     /* Synchronous */ synchronous,
	     /* Serv Name */   service_name,
	     /* AC name */     ac_name );

    fclose (f);
    ret = TRUE;
  }
  else
    ret = FALSE;
  return ret;
}


static gboolean
impl_export (NetworkManagerDialupUI *self, GSList *properties, const char *connection_name)
{
  char *suggested_name;
  char *path = NULL;
  GtkWidget *dialog;
  NetworkManagerDialupUIImpl *impl = (NetworkManagerDialupUIImpl *) self->data;

  /*printf ("in impl_export\n");*/

  dialog = gtk_file_chooser_dialog_new (_("Save as..."),
					NULL,
					GTK_FILE_CHOOSER_ACTION_SAVE,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					NULL);

  suggested_name = g_strdup_printf ("%s.nmd", connection_name);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), suggested_name);
  g_free (suggested_name);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
  {

    path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
/*     printf ("User selected '%s'\n", path); */

  }

  gtk_widget_destroy (dialog);

  if (path != NULL) {
    if (g_file_test (path, G_FILE_TEST_EXISTS)) {
      int response;

      dialog = gtk_message_dialog_new (NULL,
				       GTK_DIALOG_DESTROY_WITH_PARENT,
				       GTK_MESSAGE_QUESTION,
				       GTK_BUTTONS_CANCEL,
				       _("A file named \"%s\" already exists."), path);
      gtk_dialog_add_buttons (GTK_DIALOG (dialog), "_Replace", GTK_RESPONSE_OK, NULL);
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						_("Do you want to replace it with the one you are saving?"));
      response = gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      if (response != GTK_RESPONSE_OK)
	goto out;
    }

    if (!export_to_file (impl, path, properties, connection_name)) {

      dialog = gtk_message_dialog_new (NULL,
				       GTK_DIALOG_DESTROY_WITH_PARENT,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_CLOSE,
				       _("Failed to export configuration"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						_("Failed to save file %s"), path);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
  }

out:
  g_free (path);

  return TRUE;
}


static NetworkManagerDialupUI*
impl_get_object (void)
{
  char *glade_file;
  NetworkManagerDialupUIImpl *impl;

  impl = g_new0 (NetworkManagerDialupUIImpl, 1);

  glade_file = g_strdup_printf ("%s/%s", GLADEDIR, "nm-pppoe-dialog.glade");
  impl->xml = glade_xml_new (glade_file, NULL, GETTEXT_PACKAGE);
  g_free( glade_file );
  if (impl->xml != NULL) {
    GtkListStore *store = NULL;

    impl->widget = glade_xml_get_widget(impl->xml, "nm-pppoe-widget");

    impl->w_connection_name        = GTK_ENTRY (glade_xml_get_widget (impl->xml, "pppoe-connection-name"));
    impl->w_interface              = GTK_COMBO_BOX_ENTRY (glade_xml_get_widget (impl->xml, "pppoe-interface"));
    impl->w_service_name           = GTK_ENTRY (glade_xml_get_widget (impl->xml, "pppoe-service-name"));
    impl->w_ac_name                = GTK_ENTRY (glade_xml_get_widget (impl->xml, "pppoe-ac-name"));
    impl->w_synchronous            = GTK_CHECK_BUTTON (glade_xml_get_widget (impl->xml, "pppoe-synchronous"));

    impl->w_opt_info_expander      = GTK_EXPANDER (glade_xml_get_widget (impl->xml, "pppoe-optional-information-expander"));

    impl->w_import_button          = GTK_BUTTON (glade_xml_get_widget (impl->xml, "pppoe-import"));

    impl->callback                 = NULL;

    store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_combo_box_set_model (GTK_COMBO_BOX(impl->w_interface), GTK_TREE_MODEL(store));
    gtk_combo_box_entry_set_text_column (impl->w_interface, 0);
    g_object_unref (G_OBJECT (store));


    gtk_signal_connect (GTK_OBJECT (impl->w_connection_name),
			"changed", GTK_SIGNAL_FUNC (editable_changed), impl);
    gtk_signal_connect (GTK_OBJECT (impl->w_interface),
			"changed", GTK_SIGNAL_FUNC (editable_changed), impl);
    gtk_signal_connect (GTK_OBJECT (impl->w_service_name),
			"changed", GTK_SIGNAL_FUNC (editable_changed), impl);
    gtk_signal_connect (GTK_OBJECT (impl->w_ac_name),
			"changed", GTK_SIGNAL_FUNC (editable_changed), impl);
    gtk_signal_connect (GTK_OBJECT (impl->w_import_button),
			"clicked", GTK_SIGNAL_FUNC (import_button_clicked), impl);

    /* make the widget reusable */
    gtk_signal_connect (GTK_OBJECT (impl->widget), "delete-event",
			GTK_SIGNAL_FUNC (gtk_widget_hide_on_delete), NULL);

    pppoe_clear_widget (impl);

    impl->parent.get_display_name              = impl_get_display_name;
    impl->parent.get_service_name              = impl_get_service_name;
    impl->parent.get_service_type              = impl_get_service_type;
    impl->parent.get_widget                    = impl_get_widget;
    impl->parent.get_connection_name           = impl_get_connection_name;
    impl->parent.get_properties                = impl_get_properties;
    impl->parent.set_validity_changed_callback = impl_set_validity_changed_callback;
    impl->parent.set_dbus_connection           = impl_set_dbus_connection;
    impl->parent.is_valid                      = impl_is_valid;
    impl->parent.get_confirmation_details      = impl_get_confirmation_details;
    impl->parent.can_export                    = impl_can_export;
    impl->parent.import_file                   = impl_import_file;
    impl->parent.export                        = impl_export;
    impl->parent.data                          = impl;

    return &(impl->parent);
  } else {
    g_free (impl);
    return NULL;
  }
}

NetworkManagerDialupUI * nm_dialup_properties_factory(void);

NetworkManagerDialupUI *
nm_dialup_properties_factory (void)
{
  return impl_get_object();
}
