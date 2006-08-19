/* NetworkManager -- Authentication dialog for ppp connections
 *
 * Tim Niemueller <tim@niemueller.de>
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
 * (C) Copyright 2004      Red Hat, Inc.
 *               2005-2006 Tim Niemueller [www.niemueller.de]
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomeui/libgnomeui.h>
#include <gconf/gconf-client.h>
#include <gnome-keyring.h>

#include "gnome-two-password-dialog.h"

#define PPP_SERVICE "org.freedesktop.NetworkManager.ppp"

// MUST be the same as in gnome/applet/applet.h
// A real fix for this is needed by giving more information to auth apps
#define GCONF_PATH_DIALUP_CONNECTIONS "/system/networking/dialup_connections"

static GSList *
lookup_user_pass (const char *dialup_name, const char *dialup_service, gboolean *is_session)
{
  GSList *userpass;
  GList *keyring_result;
  GList *keyring_i;

  char *password = NULL;
  char *user     = NULL;

  userpass = NULL;

  if (gnome_keyring_find_network_password_sync (NULL,                   /* user */
						NULL,                   /* domain */
						dialup_name,            /* server */
						NULL,                   /* object */
						dialup_service,         /* protocol */
						NULL,                   /* authtype */
						0,                      /* port */
						&keyring_result) != GNOME_KEYRING_RESULT_OK)
    return FALSE;

  *is_session = FALSE;

  // Go through all passwords and assign to appropriate variable
  for (keyring_i = keyring_result; keyring_i != NULL; keyring_i = g_list_next (keyring_i)) {
    
    GnomeKeyringNetworkPasswordData *data = keyring_i->data;
      
    if (strcmp (data->object, "password") == 0) {
      user     = data->user;
      password = data->password;
    }

    if (strcmp (data->keyring, "session") == 0)
      *is_session = TRUE;
    
  }

  if (user != NULL) {
    userpass = g_slist_append (userpass, g_strdup (user));
  } else {
    userpass = g_slist_append (userpass, g_strdup (""));
  }

  if (password != NULL) {
    userpass = g_slist_append (userpass, g_strdup (password));
  } else {
    userpass = g_slist_append (userpass, g_strdup (""));
  }

  gnome_keyring_network_password_list_free (keyring_result);

  return userpass;
}

static void
save_ppp_username_and_password (const char *dialup_name, const char *dialup_service,
			    const char *keyring,
			    const char *username, const char *password)
{
  guint32 item_id;
  GnomeKeyringResult keyring_result;

  if ( password != NULL) {
    keyring_result = gnome_keyring_set_network_password_sync (keyring,
							      username,
							      NULL,
							      dialup_name,
							      "password",
							      dialup_service,
							      NULL,
							      0,
							      password,
							      &item_id);
    if (keyring_result != GNOME_KEYRING_RESULT_OK) {
      const char *s;
      switch (keyring_result) {
      case GNOME_KEYRING_RESULT_DENIED:
	s = "GNOME_KEYRING_RESULT_DENIED";
	break;

      case GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON:
	s = "GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON";
	break;

      case GNOME_KEYRING_RESULT_ALREADY_UNLOCKED:
	s = "GNOME_KEYRING_RESULT_ALREADY_UNLOCKED";
	break;

      case GNOME_KEYRING_RESULT_NO_SUCH_KEYRING:
	s = "GNOME_KEYRING_RESULT_NO_SUCH_KEYRING";
	break;

      case GNOME_KEYRING_RESULT_BAD_ARGUMENTS:
	s = "GNOME_KEYRING_RESULT_BAD_ARGUMENTS";
	break;

      case GNOME_KEYRING_RESULT_IO_ERROR:
	s = "GNOME_KEYRING_RESULT_IO_ERROR";
	break;

      case GNOME_KEYRING_RESULT_CANCELLED:
	s = "GNOME_KEYRING_RESULT_CANCELLED";
	break;

      case GNOME_KEYRING_RESULT_ALREADY_EXISTS:
	s = "GNOME_KEYRING_RESULT_ALREADY_EXISTS";
	break;

      default:
	s = "unknown";
      }

      g_warning ("Couldn't store password in keyring, code %d (%s)", (int) keyring_result, s);
    }
  }

}


static GSList *
get_username_password (const char *dialup_name,
		       const char *dialup_service,
		       gboolean retry)
{
  GSList          *result = NULL;
  char            *prompt;
  GtkWidget	  *dialog;
  GSList          *keyring_result = NULL;
  char            *keyring_username = NULL;
  char            *keyring_password = NULL;
  gboolean         keyring_is_session;
  GnomeTwoPasswordDialogRemember remember;

  g_return_val_if_fail (dialup_name != NULL, NULL);

  if (!retry) {
    if ((result = lookup_user_pass (dialup_name, dialup_service, &keyring_is_session)) != NULL) {
      return result;
    }
  } else {
    if ((keyring_result = lookup_user_pass (dialup_name, dialup_service, &keyring_is_session)) != NULL) {
      keyring_username = g_strdup ((char *) keyring_result->data);
      keyring_password = g_strdup ((char *) g_slist_next (keyring_result)->data );
    }
    g_slist_foreach (keyring_result, (GFunc)g_free, NULL);
    g_slist_free (keyring_result);
  }

  prompt = g_strdup_printf (_("You need to authenticate to establish the dialup connection '%s'."), dialup_name);
  dialog = gnome_two_password_dialog_new (_("Authenticate PPP"), prompt, NULL, NULL, FALSE);
  g_free (prompt);

  gnome_two_password_dialog_set_show_username (GNOME_TWO_PASSWORD_DIALOG (dialog), TRUE);
  gnome_two_password_dialog_set_show_userpass_buttons (GNOME_TWO_PASSWORD_DIALOG (dialog), FALSE);
  gnome_two_password_dialog_set_show_domain (GNOME_TWO_PASSWORD_DIALOG (dialog), FALSE);
  gnome_two_password_dialog_set_show_remember (GNOME_TWO_PASSWORD_DIALOG (dialog), TRUE);
  gnome_two_password_dialog_set_show_password_secondary (GNOME_TWO_PASSWORD_DIALOG (dialog), FALSE);

  /* if retrying, put in the passwords from the keyring */
  if (keyring_username != NULL) {
    gnome_two_password_dialog_set_username (GNOME_TWO_PASSWORD_DIALOG (dialog),
					    keyring_username);
  }
  if (keyring_password != NULL) {
    gnome_two_password_dialog_set_password (GNOME_TWO_PASSWORD_DIALOG (dialog),
					    keyring_password);
  }

  /* use the same keyring storage options as from the items we put in the entry boxes */
  remember = GNOME_TWO_PASSWORD_DIALOG_REMEMBER_NOTHING;
  if (keyring_result != NULL) {
    if (keyring_is_session)
      remember = GNOME_TWO_PASSWORD_DIALOG_REMEMBER_SESSION;
    else
      remember = GNOME_TWO_PASSWORD_DIALOG_REMEMBER_FOREVER;				
  }
  gnome_two_password_dialog_set_remember (GNOME_TWO_PASSWORD_DIALOG (dialog), remember);

  gtk_widget_show (dialog);

  if (gnome_two_password_dialog_run_and_block (GNOME_TWO_PASSWORD_DIALOG (dialog))) {
    char *password;
    char *username;

    username = gnome_two_password_dialog_get_username (GNOME_TWO_PASSWORD_DIALOG (dialog));
    password = gnome_two_password_dialog_get_password (GNOME_TWO_PASSWORD_DIALOG (dialog));

    result = g_slist_append (result, g_strdup (username));
    result = g_slist_append (result, g_strdup (password));

    switch (gnome_two_password_dialog_get_remember (GNOME_TWO_PASSWORD_DIALOG (dialog))) {
    case GNOME_TWO_PASSWORD_DIALOG_REMEMBER_SESSION:
      save_ppp_username_and_password (dialup_name, dialup_service, "session", username, password);
      break;
    case GNOME_TWO_PASSWORD_DIALOG_REMEMBER_FOREVER:
      save_ppp_username_and_password (dialup_name, dialup_service, NULL, username, password);
      break;
    default:
      break;
    }
  }

  g_free (keyring_username);
  g_free (keyring_password);
  gtk_widget_destroy (dialog);

  return result;
}

int 
main (int argc, char *argv[])
{
  GConfClient *gconf_client = NULL;
  GConfValue  *gconf_val = NULL;
  gchar       *gconf_key = NULL;
  char        *escaped_name;
  gboolean     valid_conn = FALSE;
  GSList      *i;
  GSList      *userpass;
  static gboolean  retry = FALSE;
  static gchar    *dialup_name = NULL;
  static gchar    *dialup_service = NULL;
  GError          *error = NULL;
  GOptionContext  *context;
  int          bytes_read;
  static GOptionEntry entries[] = 
    {
      { "reprompt", 'r', 0, G_OPTION_ARG_NONE, &retry, "Reprompt for passwords", NULL},
      { "name", 'n', 0, G_OPTION_ARG_STRING, &dialup_name, "Name of dialup connection", NULL},
      { "service", 's', 0, G_OPTION_ARG_STRING, &dialup_service, "Dialup service type", NULL},
      { NULL }
    };
  char buf[1];

  bindtextdomain (GETTEXT_PACKAGE, NULL);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  userpass = NULL;

  context = g_option_context_new ("- ppp auth dialog");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  g_option_context_parse (context, &argc, &argv, &error);

  if (dialup_name == NULL || dialup_service == NULL) {
    fprintf (stderr, "Have to supply both dialup name and service\n");
    goto out;
  }

  if (strcmp (dialup_service, PPP_SERVICE) != 0) {
    fprintf (stderr, "This dialog only works with the '%s' service\n", PPP_SERVICE);
    goto out;		
  }

  gnome_program_init ("nm-ppp-auth-dialog", VERSION, LIBGNOMEUI_MODULE,
		      argc, argv, 
		      GNOME_PARAM_NONE, GNOME_PARAM_NONE);


  gconf_client = gconf_client_get_default();
  escaped_name = gconf_escape_key (dialup_name, strlen (dialup_name));
  gconf_key    = g_strdup_printf ("%s/%s/dialup_data", GCONF_PATH_DIALUP_CONNECTIONS, escaped_name);
  if ( !(gconf_val = gconf_client_get (gconf_client, gconf_key, NULL)) ||
       !(gconf_val->type == GCONF_VALUE_LIST) ||
       !(gconf_value_get_list_type (gconf_val) == GCONF_VALUE_STRING)) {

    if (gconf_val)
      gconf_value_free (gconf_val);
    g_free (gconf_key);

    fprintf( stderr, "There is no connection named '%s'\n", dialup_name);
    goto out;
  }
  g_free (gconf_key);

  valid_conn = TRUE;
    
  for (i = gconf_value_get_list (gconf_val); i != NULL; i = g_slist_next (i)) {
    const char *gkey = gconf_value_get_string ((GConfValue *)i->data);
    const char *gval = NULL;

    i = g_slist_next (i);
    if (i != NULL) {
      gval = gconf_value_get_string ((GConfValue *)i->data);
    }

    if ( gkey != NULL ) {

      /* if something special has to happen...
      if ( strcmp (gkey, "magic-val") == 0 ) {
        // do something spooky
      } else if ( strcmp (gkey, "key") == 0 ) {
	key = g_strdup (gval);
      }
      */
    }
  }
  gconf_value_free (gconf_val);

  userpass = get_username_password (dialup_name, dialup_service, retry);
  if (userpass == NULL)
    goto out;

  /* dump the passwords to stdout */
  for (i = userpass; i != NULL; i = g_slist_next (i)) {
    char *item = (char *) i->data;
    printf ("%s\n", item);
  }

  g_slist_foreach (userpass, (GFunc)g_free, NULL);
  g_slist_free (userpass);

  printf ("\n\n");
  /* for good measure, flush stdout since Kansas is going Bye-Bye */
  fflush (stdout);

  /* wait for data on stdin  */
  bytes_read = fread (buf, sizeof (char), sizeof (buf), stdin);

 out:
  g_object_unref (gconf_client);
  g_option_context_free (context);

  if ( ! valid_conn ) {
    return 1;
  } else {
    return (userpass != NULL) ? 0 : 1;
  }
}
