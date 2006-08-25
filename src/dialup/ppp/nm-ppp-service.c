/* nm-ppp-supervisor - pppd supervisor
 *
 * Tim Niemueller <tim@niemueller.de>
 * Based on work by Dan Williams <dcbw@redhat.com>
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
 * $Id$
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#include <NetworkManager/NetworkManager.h>

#include "nm-ppp-service.h"
#include "nm-utils.h"

#define NM_PPPD_PLUGIN		"nm-pppd-plugin.so"
#define NM_PPPOE_PIDFILE        LOCALSTATEDIR"/run/NetworkManager-pppoe.pid"

typedef struct _NmPPP_IOData
{
  char           *username;
  char           *password;
  gint            child_stdin_fd;
  gint            child_stdout_fd;
  gint            child_stderr_fd;
} NmPPP_IOData;

typedef struct _NmPPPData
{
  GMainLoop            *loop;
  DBusConnection       *con;
  NMDialupState		state;
  gboolean              use_wvdial;
  gboolean              use_pppoe;
  GPid			pid;
  guint			quit_timer;
  guint			helper_timer;
  gint                  connection_type;
  guint                 connect_timer;
  guint                 connect_count;
  NmPPP_IOData         *io_data;
} NmPPPData;

static gboolean nm_ppp_dbus_handle_stop_pppd (NmPPPData *data);


static inline const char *nm_find_pppd (void)
{
  static const char *pppd_binary_paths[] =
    {
      "/usr/local/sbin/pppd",
      "/usr/sbin/pppd",
      "/sbin/pppd",
      NULL
    };

  const char  **pppd_binary = pppd_binary_paths;

  while (*pppd_binary != NULL) {
    if (g_file_test (*pppd_binary, G_FILE_TEST_EXISTS))
      break;
    pppd_binary++;
  }

  return *pppd_binary;
}


static inline const char *nm_find_wvdial (void)
{
  static const char *wvdial_binary_paths[] =
    {
      "/usr/local/bin/wvdial",
      "/usr/bin/wvdial",
      "/bin/wvdial",
      NULL
    };

  const char  **wvdial_binary = wvdial_binary_paths;

  while (*wvdial_binary != NULL) {
    if (g_file_test (*wvdial_binary, G_FILE_TEST_EXISTS))
      break;
    wvdial_binary++;
  }

  return *wvdial_binary;
}


/*
 * nm_dbus_create_error_message
 *
 * Make a DBus error message
 *
 */
static DBusMessage *
nm_dbus_create_error_message (DBusMessage *message, const char *exception_namespace,
			      const char *exception, const char *format, ...)
{
  char *exception_text;
  DBusMessage	*reply;
  va_list		 args;
  char			 error_text[512];

  va_start (args, format);
  vsnprintf (error_text, 512, format, args);
  va_end (args);

  exception_text = g_strdup_printf ("%s.%s", exception_namespace, exception);
  reply = dbus_message_new_error (message, exception_text, error_text);
  g_free (exception_text);
  
  return (reply);
}


/*
 * nm_ppp_dbus_signal_failure
 *
 * Signal the bus that some PPP operation failed.
 *
 */
static void
nm_ppp_dbus_signal_failure (NmPPPData *data, const char *sig)
{
  DBusMessage	*message;
  const char	*error_msg = NULL;

  g_return_if_fail (data != NULL);
  g_return_if_fail (sig != NULL);

  if ( strcmp (sig, NM_DBUS_DIALUP_SIGNAL_LOGIN_FAILED) == 0 )
    error_msg = _("The dialup login failed because the user name or password were not accepted.");
  else if (strcmp (sig, NM_DBUS_DIALUP_SIGNAL_LAUNCH_FAILED) == 0 )
    error_msg = _("The dialup login failed because the dialup program could not be started.");
  else if (strcmp (sig, NM_DBUS_DIALUP_SIGNAL_CONNECT_FAILED) == 0 )
    error_msg = _("The dialup login failed because the dialup program could not connect to your internet service provider.");
  else if (strcmp (sig, NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD) == 0 )
    error_msg = _("The dialup login failed because the configuration options were invalid.");
  else if (strcmp (sig, NM_DBUS_DIALUP_SIGNAL_IP_CONFIG_BAD) == 0 )
    error_msg = _("The dialup login failed because the dialup program received an invalid configuration from the dialup server.");
  else
    error_msg = _("Dialup connection failed");

  if (!error_msg)
    return;

  if (!(message = dbus_message_new_signal (NM_DBUS_PATH_PPP, NM_DBUS_INTERFACE_PPP, sig)))
    {
      nm_warning ("Not enough memory for new dbus message!");
      return;
    }

  dbus_message_append_args (message, DBUS_TYPE_STRING, &error_msg, DBUS_TYPE_INVALID);
  if (!dbus_connection_send (data->con, message, NULL))
    nm_warning ("Could not raise the signal!");

  dbus_message_unref (message);
}


/*
 * nm_ppp_dbus_signal_state_change
 *
 * Signal the bus that our state changed.
 *
 */
static void
nm_ppp_dbus_signal_state_change (NmPPPData *data, NMDialupState old_state)
{
  DBusMessage	*message;

  g_return_if_fail (data != NULL);

  if (!(message = dbus_message_new_signal (NM_DBUS_PATH_PPP, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_SIGNAL_STATE_CHANGE)))
    {
      nm_warning ("nm_openppp_dbus_signal_state_change(): Not enough memory for new dbus message!");
      return;
    }

  dbus_message_append_args (message, DBUS_TYPE_UINT32, &old_state, DBUS_TYPE_UINT32, &(data->state), DBUS_TYPE_INVALID);

  nm_info("nm_ppp_dbus_signal_state_change(): signaling state change %d -> %d", old_state, data->state);

  if (!dbus_connection_send (data->con, message, NULL))
    nm_warning ("nm_ppp_dbus_signal_state_change(): Could not raise the signal!");

  dbus_message_unref (message);
}


/*
 * nm_ppp_set_state
 *
 * Set our state and make sure to signal the bus.
 *
 */
static void
nm_ppp_set_state (NmPPPData *data, NMDialupState new_state)
{
  NMDialupState	old_state;

  g_return_if_fail (data != NULL);

  old_state = data->state;

  if (old_state != new_state)
    {
      data->state = new_state;
      nm_ppp_dbus_signal_state_change (data, old_state);
    }
}


/*
 * nm_ppp_quit_timer_cb
 *
 * Callback to quit nm-ppp-service after a certain period of time.
 *
 */
static gboolean
nm_ppp_quit_timer_cb (NmPPPData *data)
{
  data->quit_timer = 0;

  g_return_val_if_fail (data != NULL, FALSE);

  g_main_loop_quit (data->loop);

  return FALSE;
}


/*
 * nm_ppp_schedule_quit_timer
 *
 * If ppp isn't running, and we haven't been asked to do anything in a while,
 * then we just exit since NetworkManager will re-launch us later.
 *
 */
static void
nm_ppp_schedule_quit_timer (NmPPPData *data, guint interval)
{
  g_return_if_fail (data != NULL);

  if (data->quit_timer == 0)
    data->quit_timer = g_timeout_add (interval, (GSourceFunc) nm_ppp_quit_timer_cb, data);
}


/*
 * nm_ppp_cancel_quit_timer
 *
 * Cancel a quit timer that we've scheduled before.
 *
 */
static void
nm_ppp_cancel_quit_timer (NmPPPData *data)
{
  g_return_if_fail (data != NULL);

  if (data->quit_timer > 0)
    g_source_remove (data->quit_timer);
}




/*
 * nm_ppp_helper_timer_cb
 *
 * If we haven't received the IP4 config info from the helper before the timeout
 * occurs, we kill ppp
 *
 */
static gboolean
nm_ppp_helper_timer_cb (NmPPPData *data)
{
  data->helper_timer = 0;

  g_return_val_if_fail (data != NULL, FALSE);

  nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_CONNECT_FAILED);
  nm_warning("4");
  nm_ppp_dbus_handle_stop_pppd (data);

  return FALSE;
}


/*
 * nm_ppp_schedule_helper_timer
 *
 * Once ppp is running, we wait for the helper to return the IP4 configuration
 * information to us.  If we don't receive that information within 7 seconds,
 * we kill ppp
 *
 */
static void
nm_ppp_schedule_helper_timer (NmPPPData *data)
{
  g_return_if_fail (data != NULL);

  if (data->helper_timer == 0)
    data->helper_timer = g_timeout_add (50000, (GSourceFunc) nm_ppp_helper_timer_cb, data);
}


/*
 * nm_ppp_cancel_helper_timer
 *
 * Cancel a helper timer that we've scheduled before.
 *
 */
static void
nm_ppp_cancel_helper_timer (NmPPPData *data)
{
  g_return_if_fail (data != NULL);

  if (data->helper_timer > 0)
    g_source_remove (data->helper_timer);
}



/*
 * ppp_watch_cb
 *
 * Watch our child ppp process and get notified of events from it.
 *
 */
static void
ppp_watch_cb (GPid pid, gint status, gpointer user_data)
{
  guint	error = -1;

  NmPPPData *data = (NmPPPData *)user_data;

  if (WIFEXITED (status))
    {
      error = WEXITSTATUS (status);
      if (error != 0)
	nm_warning ("ppp exited with error code %d", error);
    }
  else if (WIFSTOPPED (status))
    nm_warning ("ppp stopped unexpectedly with signal %d", WSTOPSIG (status));
  else if (WIFSIGNALED (status))
    nm_warning ("ppp died with signal %d", WTERMSIG (status));
  else
    nm_warning ("ppp died from an unknown cause");
  
  /* Reap child if needed. */
  waitpid (data->pid, NULL, WNOHANG);
  data->pid = 0;

  /* Must be after data->state is set since signals use data->state */
  /* This is still code from vpnc, ppp does not supply useful exit codes :-/ */
  switch (error)
    {
    case 2:	/* Couldn't log in due to bad user/pass */
      nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_LOGIN_FAILED);
      break;

    case 1:	/* Other error (couldn't bind to address, etc) */
      nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_CONNECT_FAILED);
      break;

    default:
      break;
    }

  nm_ppp_set_state (data, NM_DIALUP_STATE_STOPPED);
  nm_ppp_schedule_quit_timer (data, 10000);
}



/*
 * write_config_option
 *
 * Helper that writes a formatted string to an fd
 *
 */
static inline void
write_config_option (int fd, const char *format, ...)
{
  char * 	string;
  va_list	args;
  int		x;

  va_start (args, format);
  string = g_strdup_vprintf (format, args);
  printf("Writing config: %s", string);
  x = write (fd, string, strlen (string));
  g_free (string);
  va_end (args);
}


/*
 * nm_ppp_start_pppd_binary
 *
 * Start the ppp binary with a set of arguments and a config file.
 *
 */
static gint
nm_ppp_start_pppd_binary (NmPPPData *data,
				 char **data_items, const int num_items,
				 char **userpass, const int num_userpass
				 )
{
  GPid	        pid;
  const char   *ppp_binary = NULL;
  GError       *error = NULL;
  GSource      *ppp_watch;
  gint	        stdin_fd = -1;
  gint          stdout_fd = -1;
  gint          stderr_fd = -1;
  int           i = 0;

  char         *baudrate = NULL;
  char         *device = NULL;
  char         *flowcontrol = NULL;
  char         *plugin = NULL;
  char         *number = NULL;
  char         *init_1 = NULL;
  char         *init_2 = NULL;
  char         *init_3 = NULL;
  char         *init_4 = NULL;
  char         *init_5 = NULL;
  char         *init_6 = NULL;
  char         *init_7 = NULL;
  char         *init_8 = NULL;
  char         *init_9 = NULL;
  char         *volume = "0";
  char         *pty    = NULL;
  char         *mtu    = NULL;
  char         *mru    = NULL;
  char         *asyncmap = NULL;
  gboolean      use_lock = FALSE;
  gboolean      use_comp_vjheader = FALSE;
  gboolean      use_comp_vjcid = FALSE;
  gboolean      use_comp_acc = FALSE;
  gboolean      use_comp_pfc = FALSE;
  gboolean      use_comp_deflate = FALSE;
  gboolean      use_comp_bsd = FALSE;
  gboolean      use_comp_ccp = FALSE;
  char         *lcp_echo_interval = NULL;
  char         *lcp_echo_failure = NULL;
  char         *pppoe_stt = NULL;
  char         *pppoe_mss = NULL;
  char         *pppoe_service_name = NULL;
  char         *pppoe_ac_name = NULL;
  char         *pppoe_interface = NULL;
  gboolean      pppoe_synchronous = FALSE;

  g_return_val_if_fail (data != NULL, -1);

  data->pid = 0;

  nm_info ("Starting binary");

  if ( (num_items == 0) || (data_items == NULL) ) {
    return -1;
  }

  /* Find ppp, it's always needed */
  ppp_binary = nm_find_pppd();
  if (!ppp_binary) {
    nm_info ("Could not find ppp binary.");
    return -1;
  }

  // Note that it should be guaranteed that num_items % 2 == 0
  // Parse parameters
  for (i = 0; i < num_items; ++i) {
    /* wvdial */
    if ( (strcmp( data_items[i], "wvdial" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      if ( data->use_wvdial ) {
	nm_warning ("Wvdial and PPPoE cannot be used at the same time!");
	return -2;
      }
      if ( pty != NULL ) {
	nm_warning ("Custom pty cannot be used with wvdial or PPPoE!");
	return -4;
      }
      data->use_wvdial = TRUE;

    /* pppoe */
    } else if ( (strcmp( data_items[i], "pppoe" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      if ( data->use_pppoe ) {
	nm_warning ("Wvdial and PPPoE cannot be used at the same time!");
	return -2;
      }
      if ( pty != NULL ) {
	nm_warning ("Custom pty cannot be used with wvdial or PPPoE!");
	return -4;
      }
      data->use_pppoe = TRUE;

    /* pty */
    } else if ( strcmp( data_items[i], "pty" ) == 0) {
      if ( device != NULL ) {
	nm_warning ("Device and a pty cannot be supplied at the same time!");
	return -3;
      }
      if ( data->use_wvdial || data->use_pppoe ) {
	nm_warning ("Custom pty cannot be used with wvdial or PPPoE!");
	return -4;
      }
      pty = data_items[++i];

    } else if ( strcmp( data_items[i], "mtu" ) == 0) {
      mtu = data_items[++i];
    } else if ( strcmp( data_items[i], "mru" ) == 0) {
      mru = data_items[++i];
    } else if ( strcmp( data_items[i], "baudrate" ) == 0) {
      baudrate = data_items[++i];
    } else if ( strcmp( data_items[i], "number" ) == 0) {
      number = data_items[++i];
    } else if ( strcmp( data_items[i], "volume" ) == 0) {
      volume = data_items[++i];
    } else if ( strcmp( data_items[i], "flowcontrol" ) == 0) {
      ++i;
      if ( (strcmp( data_items[i], "crtscts" ) == 0) ||
	   (strcmp( data_items[i], "xonxoff" ) == 0) ) {
	flowcontrol = data_items[i];
      }
    } else if ( strcmp( data_items[i], "plugin" ) == 0) {
      plugin = data_items[++i];
    } else if ( (strcmp( data_items[i], "lock" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_lock = TRUE;
    } else if ( strcmp( data_items[i], "device" ) == 0) {
      if ( pty != NULL ) {
	nm_warning ("Device and a pty cannot be supplied at the same time!");
	return -3;
      }
      device = data_items[++i];
    } else if ( strcmp( data_items[i], "asyncmap" ) == 0) {
      asyncmap = data_items[++i];
    } else if ( strcmp( data_items[i], "init_1" ) == 0) {
      init_1 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_2" ) == 0) {
      init_2 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_3" ) == 0) {
      init_3 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_4" ) == 0) {
      init_4 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_5" ) == 0) {
      init_5 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_6" ) == 0) {
      init_6 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_7" ) == 0) {
      init_7 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_8" ) == 0) {
      init_8 = data_items[++i];
    } else if ( strcmp( data_items[i], "init_9" ) == 0) {
      init_9 = data_items[++i];
    } else if ( (strcmp( data_items[i], "comp-vjheader" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_vjheader = TRUE;
    } else if ( (strcmp( data_items[i], "comp-vjcid" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_vjcid = TRUE;
    } else if ( (strcmp( data_items[i], "comp-acc" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_acc = TRUE;
    } else if ( (strcmp( data_items[i], "comp-pfc" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_pfc = TRUE;
    } else if ( (strcmp( data_items[i], "comp-deflate" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_deflate = TRUE;
    } else if ( (strcmp( data_items[i], "comp-bsd" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_bsd = TRUE;
    } else if ( (strcmp( data_items[i], "comp-ccp" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      use_comp_ccp = TRUE;
    } else if ( strcmp( data_items[i], "lcp_echo_interval" ) == 0) {
      lcp_echo_interval = data_items[++i];
    } else if ( strcmp( data_items[i], "lcp_echo_failure" ) == 0) {
      lcp_echo_failure = data_items[++i];
    } else if ( strcmp( data_items[i], "pppoe-session-traffic-timeout" ) == 0) {
      pppoe_stt = data_items[++i];
    } else if ( strcmp( data_items[i], "pppoe-mss" ) == 0) {
      pppoe_mss = data_items[++i];
    } else if ( strcmp( data_items[i], "pppoe-service-name" ) == 0) {
      pppoe_service_name = data_items[++i];
    } else if ( strcmp( data_items[i], "pppoe-ac-name" ) == 0) {
      pppoe_ac_name = data_items[++i];
    } else if ( strcmp( data_items[i], "pppoe-interface" ) == 0) {
      pppoe_interface = data_items[++i];
    } else if ( (strcmp( data_items[i], "pppoe-synchronous" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      pppoe_synchronous = TRUE;
    }

  }

  if ( data->use_wvdial ) {
    GPtrArray    *wvdial_argv = NULL;

    // Find wvdial, it's always needed
    const char *wvdial_binary = nm_find_wvdial();
    if (!wvdial_binary) {
      nm_info ("Could not find wvdial binary.");
      return -1;
    }

    nm_info ("Starting wvdial connection");

    wvdial_argv = g_ptr_array_new ();

    // create wvdial command line
    g_ptr_array_add (wvdial_argv, (gpointer) (wvdial_binary));
    g_ptr_array_add (wvdial_argv, "--config-stdin");
    g_ptr_array_add (wvdial_argv, NULL);


    if (!g_spawn_async_with_pipes (NULL, (char **) wvdial_argv->pdata, NULL,
				   G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &stdin_fd,
				   &stdout_fd, &stderr_fd, &error)) {

      g_ptr_array_free (wvdial_argv, TRUE);
      nm_warning ("ppp failed to start.  error: '%s'", error->message);
      g_error_free(error);
      return -1;
    } else {
      
      GString *fc = g_string_new( flowcontrol );
      fc = g_string_ascii_up( fc );

      write_config_option (stdin_fd, "[Dialer Defaults]\n");
      write_config_option (stdin_fd, "Modem = %s\n", device);
      write_config_option (stdin_fd, "Baud = %s\n", baudrate);
      write_config_option (stdin_fd, "SetVolume = %s\n", volume);
      write_config_option (stdin_fd, "Dial Command = %s\n", "ATDT");
      write_config_option (stdin_fd, "Username = %s\n", userpass[0]);
      write_config_option (stdin_fd, "Password = %s\n", userpass[1]);
      write_config_option (stdin_fd, "Phone = %s\n", number);

      if ( flowcontrol != NULL ) {
	write_config_option (stdin_fd, "FlowControl = %s\n", fc->str);
      }
      if ( init_1 != NULL ) {
	write_config_option (stdin_fd, "Init1 = %s\n", init_1);
      }
      if ( init_2 != NULL ) {
	write_config_option (stdin_fd, "Init2 = %s\n", init_2);
      }
      if ( init_3 != NULL ) {
	write_config_option (stdin_fd, "Init3 = %s\n", init_3);
      }
      if ( init_4 != NULL ) {
	write_config_option (stdin_fd, "Init4 = %s\n", init_4);
      }
      if ( init_5 != NULL ) {
	write_config_option (stdin_fd, "Init5 = %s\n", init_5);
      }
      if ( init_6 != NULL ) {
	write_config_option (stdin_fd, "Init6 = %s\n", init_6);
      }
      if ( init_7 != NULL ) {
	write_config_option (stdin_fd, "Init7 = %s\n", init_7);
      }
      if ( init_8 != NULL ) {
	write_config_option (stdin_fd, "Init8 = %s\n", init_8);
      }
      if ( init_9 != NULL ) {
	write_config_option (stdin_fd, "Init9 = %s\n", init_9);
      }

      write_config_option (stdin_fd, "PPPD Option 1 = %s\n", "plugin");
      write_config_option (stdin_fd, "PPPD Option 2 = %s\n", "nm-pppd-plugin.so");
      write_config_option (stdin_fd, "PPPD Option 3 = %s\n", ((use_comp_vjheader) ? "" : "novj"));
      write_config_option (stdin_fd, "PPPD Option 4 = %s\n", ((use_comp_vjcid) ? "" : "novjccomp"));
      write_config_option (stdin_fd, "PPPD Option 5 = %s\n", ((use_comp_acc) ? "" : "noaccomp"));
      write_config_option (stdin_fd, "PPPD Option 6 = %s\n", ((use_comp_pfc) ? "" : "nopcomp"));
      write_config_option (stdin_fd, "PPPD Option 7 = %s\n", ((use_comp_deflate) ? "" : "nodeflate"));
      write_config_option (stdin_fd, "PPPD Option 8 = %s\n", ((use_comp_bsd) ? "" : "nobsdcomp"));
      write_config_option (stdin_fd, "PPPD Option 9 = %s\n", ((use_comp_ccp) ? "" : "noccp"));

      g_string_free( fc, TRUE );
      g_ptr_array_free (wvdial_argv, TRUE);
    }

  } else if ( data->use_pppoe ) {
    GPtrArray    *ppp_argv;
    GPtrArray    *pppoe_argv;
    gchar *pppoe_pty;
    gchar *ppp_cmdline;

    nm_info ("Starting pppoe connection");

    if ( pppoe_interface == NULL ) {
      nm_warning ("No interface supplied for PPPoE connection");
      return -6;
    }

    // Create pppoe command line
    pppoe_argv = g_ptr_array_new ();
    g_ptr_array_add (pppoe_argv, "-p");
    g_ptr_array_add (pppoe_argv, NM_PPPOE_PIDFILE);
    g_ptr_array_add (pppoe_argv, "-U");
    g_ptr_array_add (pppoe_argv, "-I");
    g_ptr_array_add (pppoe_argv, pppoe_interface);
    if ( pppoe_synchronous ) {
      g_ptr_array_add (pppoe_argv, "-s");
    }
    if ( (pppoe_stt != NULL)  && (strlen(pppoe_stt) > 0) ) {
      g_ptr_array_add (pppoe_argv, "-T");
      g_ptr_array_add (pppoe_argv, pppoe_stt);
    }
    if ( (pppoe_mss != NULL) && (strlen(pppoe_mss) > 0) ) {
      g_ptr_array_add (pppoe_argv, "-m");
      g_ptr_array_add (pppoe_argv, pppoe_mss);
    }
    if ( (pppoe_service_name != NULL)  && (strlen(pppoe_service_name) > 0) ) {
      g_ptr_array_add (pppoe_argv, "-S");
      g_ptr_array_add (pppoe_argv, pppoe_service_name);
    }
    if ( (pppoe_ac_name != NULL) && (strlen(pppoe_ac_name) > 0) ) {
      g_ptr_array_add (pppoe_argv, "-C");
      g_ptr_array_add (pppoe_argv, pppoe_ac_name);
    }
    g_ptr_array_add (pppoe_argv, NULL);
    pppoe_pty = g_strjoinv(" ", (gchar **)pppoe_argv->pdata);
    g_ptr_array_free (pppoe_argv, TRUE);

    // Create pppd command line
    ppp_argv = g_ptr_array_new ();
    g_ptr_array_add (ppp_argv, (gpointer) (ppp_binary));
    g_ptr_array_add (ppp_argv, "nodetach");
    g_ptr_array_add (ppp_argv, "pty");
    g_ptr_array_add (ppp_argv, pppoe_pty);
    if ( (mtu != NULL) && (strlen(mtu) > 0) ) {
      g_ptr_array_add (ppp_argv, "mtu");
      g_ptr_array_add (ppp_argv, mtu);
    }
    if ( (mru != NULL) && (strlen(mru) > 0) ) {
      g_ptr_array_add (ppp_argv, "mru");
      g_ptr_array_add (ppp_argv, mru);
    }
    if ( (lcp_echo_interval != NULL) && (strlen(lcp_echo_interval) > 0) ) {
      g_ptr_array_add (ppp_argv, "lcp-echo-interval");
      g_ptr_array_add (ppp_argv, lcp_echo_interval);
    }
    if ( (lcp_echo_failure != NULL) && (strlen(lcp_echo_failure) > 0) ) {
      g_ptr_array_add (ppp_argv, "lcp-echo-failure");
      g_ptr_array_add (ppp_argv, lcp_echo_failure);
    }
    if ( pppoe_synchronous ) {
      g_ptr_array_add (ppp_argv, "sync");
    }
    g_ptr_array_add (ppp_argv, "nodeflate");
    g_ptr_array_add (ppp_argv, "novj");
    g_ptr_array_add (ppp_argv, "novjccomp");
    g_ptr_array_add (ppp_argv, "noaccomp");
    g_ptr_array_add (ppp_argv, "nopcomp");
    g_ptr_array_add (ppp_argv, "nobsdcomp");
    g_ptr_array_add (ppp_argv, "noccp");

    g_ptr_array_add (ppp_argv, "user");
    g_ptr_array_add (ppp_argv, userpass[0]);

    g_ptr_array_add (ppp_argv, NULL);

    ppp_cmdline = g_strjoinv (" ", (gchar **)ppp_argv->pdata);
    nm_warning ("Starting pppd with cmdline '%s'", ppp_cmdline);
    g_free (ppp_cmdline);
    
    if (!g_spawn_async_with_pipes (NULL, (char **) ppp_argv->pdata, NULL,
				   G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &stdin_fd,
				   &stdout_fd, &stderr_fd, &error))
      {
	g_ptr_array_free (ppp_argv, TRUE);
	nm_warning ("ppp failed to start.  error: '%s'", error->message);
	g_error_free(error);
	return -1;
      }
    g_ptr_array_free (ppp_argv, TRUE);
    g_free (pppoe_pty);
    nm_info ("ppp (pppoe) started with pid %d", pid);

  } else {
    GPtrArray    *ppp_argv;

    nm_info ("Starting plain pppd connection");

    // Create pppd command line
    ppp_argv = g_ptr_array_new ();
    g_ptr_array_add (ppp_argv, (gpointer) (ppp_binary));
    
    if ( pty != NULL ) {
      g_ptr_array_add (ppp_argv, "pty");
      g_ptr_array_add (ppp_argv, pty);
    }
    if ( device != NULL ) {
      g_ptr_array_add (ppp_argv, device);
    }
    if ( baudrate != NULL ) {
      g_ptr_array_add (ppp_argv, baudrate);
    }
    g_ptr_array_add (ppp_argv, "nodetach");
    if ( mtu != NULL ) {
      g_ptr_array_add (ppp_argv, "mtu");
      g_ptr_array_add (ppp_argv, mtu);
    }
    if ( mru != NULL ) {
      g_ptr_array_add (ppp_argv, "mru");
      g_ptr_array_add (ppp_argv, mru);
    }
    if ( plugin != NULL ) {
      g_ptr_array_add (ppp_argv, "plugin");
      g_ptr_array_add (ppp_argv, plugin);
    }
    if ( use_lock ) {
      g_ptr_array_add (ppp_argv, "lock");
    }
    if ( asyncmap != NULL ) {
      if ( strcmp(asyncmap, "default") == 0) {
	g_ptr_array_add (ppp_argv, "default-asyncmap");
      } else {
	g_ptr_array_add (ppp_argv, "asyncmap");
	g_ptr_array_add (ppp_argv, plugin);
      }
    }
    if ( lcp_echo_interval != NULL ) {
      g_ptr_array_add (ppp_argv, "lcp-echo-interval");
      g_ptr_array_add (ppp_argv, lcp_echo_interval);
    }
    if ( lcp_echo_failure != NULL ) {
      g_ptr_array_add (ppp_argv, "lcp-echo-failure");
      g_ptr_array_add (ppp_argv, lcp_echo_failure);
    }
    if ( pppoe_synchronous ) {
      g_ptr_array_add (ppp_argv, "sync");
    }
    if ( ! use_comp_vjheader ) {
      g_ptr_array_add (ppp_argv, "novj");
    }
    if ( ! use_comp_vjcid ) {
      g_ptr_array_add (ppp_argv, "novjccomp");
    }
    if ( ! use_comp_acc ) {
      g_ptr_array_add (ppp_argv, "noaccomp");
    }
    if ( ! use_comp_pfc ) {
      g_ptr_array_add (ppp_argv, "nopcomp");
    }
    if ( ! use_comp_deflate ) {
      g_ptr_array_add (ppp_argv, "nodeflate");
    }
    if ( ! use_comp_bsd ) {
      g_ptr_array_add (ppp_argv, "nobsdcomp");
    }
    if ( ! use_comp_ccp ) {
      g_ptr_array_add (ppp_argv, "noccp");
    }
    g_ptr_array_add (ppp_argv, "user");
    g_ptr_array_add (ppp_argv, userpass[0]);


    if (!g_spawn_async_with_pipes (NULL, (char **) ppp_argv->pdata, NULL,
				   G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &stdin_fd,
				   &stdout_fd, &stderr_fd, &error))
      {
	g_ptr_array_free (ppp_argv, TRUE);
	nm_warning ("ppp failed to start.  error: '%s'", error->message);
	g_error_free(error);
	return -1;
      }
    g_ptr_array_free (ppp_argv, TRUE);
    nm_info ("ppp started with pid %d", pid);
  }
    
  data->pid = pid;
  ppp_watch = g_child_watch_source_new (pid);
  g_source_set_callback (ppp_watch, (GSourceFunc) ppp_watch_cb, data, NULL);
  g_source_attach (ppp_watch, NULL);
  g_source_unref (ppp_watch);

  {
    NmPPP_IOData  *io_data;

    io_data                  = g_new0 (NmPPP_IOData, 1);
    io_data->child_stdin_fd  = stdin_fd;
    io_data->child_stdout_fd = stdout_fd;
    io_data->child_stderr_fd = stderr_fd;
    io_data->username        = g_strdup(userpass[0]);
    io_data->password        = g_strdup(userpass[1]);
  
    data->io_data = io_data;
  }


  nm_ppp_schedule_helper_timer (data);

  return stdin_fd;  
}


typedef enum OptType
{
	OPT_TYPE_UNKNOWN = 0,
	OPT_TYPE_ADDRESS,
	OPT_TYPE_ASCII,
	OPT_TYPE_INTEGER,
	OPT_TYPE_BOOL,
	OPT_TYPE_NONE
} OptType;

typedef struct Option
{
	const char *name;
	OptType type;
} Option;

/*
 * nm_ppp_config_options_validate
 *
 * Make sure the config options are sane
 *
 */
static gboolean
nm_ppp_config_options_validate (char **data_items, int num_items)
{
  Option	allowed_opts[] = {
    { "wvdial",				OPT_TYPE_BOOL    },
    { "pppoe",				OPT_TYPE_BOOL    },
    { "pty",				OPT_TYPE_BOOL    },
    { "mtu",				OPT_TYPE_INTEGER },
    { "mru",				OPT_TYPE_INTEGER },
    { "baudrate",			OPT_TYPE_INTEGER },
    { "number",				OPT_TYPE_ASCII   },
    { "volume",				OPT_TYPE_ASCII   },
    { "flowcontrol",			OPT_TYPE_ASCII   },
    { "plugin",				OPT_TYPE_ASCII   },
    { "lock",				OPT_TYPE_ASCII   },
    { "device",				OPT_TYPE_ASCII   },
    { "asyncmap",			OPT_TYPE_ASCII   },
    { "init_1",				OPT_TYPE_ASCII   },
    { "init_2",				OPT_TYPE_ASCII   },
    { "init_3",				OPT_TYPE_ASCII   },
    { "init_4",				OPT_TYPE_ASCII   },
    { "init_5",				OPT_TYPE_ASCII   },
    { "init_6",				OPT_TYPE_ASCII   },
    { "init_7",				OPT_TYPE_ASCII   },
    { "init_8",				OPT_TYPE_ASCII   },
    { "init_9",				OPT_TYPE_ASCII   },
    { "comp-vjheader",			OPT_TYPE_BOOL    },
    { "comp-vjcid",			OPT_TYPE_BOOL    },
    { "comp-acc",			OPT_TYPE_BOOL    },
    { "comp-pfc",			OPT_TYPE_BOOL    },
    { "comp-bsd",			OPT_TYPE_BOOL    },
    { "comp-ccp",			OPT_TYPE_BOOL    },
    { "lcp-echo-interval",		OPT_TYPE_INTEGER },
    { "lcp-echo-failure",		OPT_TYPE_INTEGER },
    // PPPoE specific
    { "pppoe-session-traffic-timeout",	OPT_TYPE_INTEGER },
    { "pppoe-mss",			OPT_TYPE_INTEGER },
    { "pppoe-service-name",		OPT_TYPE_ASCII   },
    { "pppoe-ac-name",			OPT_TYPE_ASCII   },
    { "pppoe-interface",		OPT_TYPE_ASCII   },
    { "pppoe-synchronous",		OPT_TYPE_BOOL    },
    { NULL,				OPT_TYPE_UNKNOWN } };
  
  unsigned int	i;

  g_return_val_if_fail (data_items != NULL, FALSE);
  g_return_val_if_fail (num_items >= 2, FALSE);

  /* Must be an even numbers of config options */
  if ((num_items % 2) != 0) {
    nm_warning ("The number of PPP config options was not even.");
    return FALSE;
  }
  
  for (i = 0; i < num_items; i += 2) {
    Option *opt = NULL;
    unsigned int t, len;
    char *opt_value;
      
    if (!data_items[i] || !data_items[i+1])  return FALSE;
    opt_value = data_items[i+1];

    /* Find the option in the allowed list */
    for (t = 0; t < sizeof (allowed_opts) / sizeof (Option); t++)
      {
	opt = &allowed_opts[t];
	if (opt->name && !strcmp (opt->name, data_items[i]))
	  break;
      }
    if (!opt->name)  /* not found */
      {
	nm_warning ("PPP option '%s' is not allowed.", data_items[i]);
	return FALSE;
      }

    /* Don't allow control characters at all */
    len = strlen (opt_value);
    for (t = 0; t < len; t++)
      {
	if (iscntrl (opt_value[t]))
	  {
	    nm_warning ("There were invalid characters in the PPP option '%s' - '%s'.", data_items[i], opt_value);
	    return FALSE;
	  }
      }

    /* Validate the option's data */
    switch (opt->type)
      {
      case OPT_TYPE_ASCII:
	/* What other characters should we reject?? */
	break;

      case OPT_TYPE_BOOL:
	if ( (strcmp( opt_value, "yes" ) != 0) &&
	     (strcmp( opt_value, "no" ) != 0) ) {
	  nm_warning ("Invalid bool option '%s'='%s'", data_items[i], opt_value);
	  return FALSE;
	}
	break;
	  
	  
      case OPT_TYPE_NONE:
	/* These have blank data */
	break;
	
      case OPT_TYPE_ADDRESS:
	/* Can be any legal hostname or IP address */
	break;

      case OPT_TYPE_INTEGER:
	break;
	
      default:
	return FALSE;
	break;
      }
  }

  return TRUE;
}


/*
 * nm_ppp_dbus_handle_start_pppd
 *
 * Parse message arguments and start the VPN connection.
 *
 */
static gboolean
nm_ppp_dbus_handle_start_pppd (DBusMessage *message, NmPPPData *data)
{
  char **		data_items = NULL;
  int		num_items = -1;
  char **		userpass_items = NULL;
  int		num_userpass = -1;
  DBusError		error;
  gboolean		success = FALSE;
  gint			ppp_fd = -1;
  char *                name = NULL;

  g_return_val_if_fail (message != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  nm_ppp_set_state (data, NM_DIALUP_STATE_STARTING);

  dbus_error_init (&error);
  if (!dbus_message_get_args (message, &error,
			      DBUS_TYPE_STRING, &name,
			      DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &userpass_items, &num_userpass,
			      DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &data_items, &num_items,
			      DBUS_TYPE_INVALID))
    {
      nm_warning ("Could not process the request because its arguments were invalid.  dbus said: '%s'", error.message);
      nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD);
      dbus_error_free (&error);
      goto out;
    }

  if ( num_userpass < 2 ) {
    nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD);
    goto out;
  }

  if (!nm_ppp_config_options_validate (data_items, num_items)) {
    nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD);
    goto out;
  }

  /* Now we can finally try to activate the PPP */
  if ((ppp_fd = nm_ppp_start_pppd_binary (data, data_items, num_items, userpass_items, num_userpass)) >= 0) {
    // Everything ok
    success = TRUE;
  }

out:
  dbus_free_string_array (data_items);
  dbus_free_string_array (userpass_items);
  if (!success)
    nm_ppp_set_state (data, NM_DIALUP_STATE_STOPPED);
  return success;
}


/*
 * nm_ppp_dbus_handle_stop_pppd
 *
 * Stop the running ppp dameon.
 *
 */
static gboolean
nm_ppp_dbus_handle_stop_pppd (NmPPPData *data)
{
  g_return_val_if_fail (data != NULL, FALSE);

  if (data->pid > 0)
    {
      nm_ppp_set_state (data, NM_DIALUP_STATE_STOPPING);

      kill (data->pid, SIGINT);
      nm_info ("Terminated ppp daemon with PID %d.", data->pid);
      data->pid = 0;

      nm_ppp_set_state (data, NM_DIALUP_STATE_STOPPED);
      nm_ppp_schedule_quit_timer (data, 10000);
    }

  return TRUE;
}


/*
 * nm_ppp_dbus_start_pppd
 *
 * Begin a PPP connection.
 *
 */
static DBusMessage *
nm_ppp_dbus_start_pppd (DBusConnection *con, DBusMessage *message, NmPPPData *data)
{
  DBusMessage		*reply = NULL;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (con != NULL, NULL);
  g_return_val_if_fail (message != NULL, NULL);

  switch (data->state)
    {
    case NM_DIALUP_STATE_STARTING:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_STARTING_IN_PROGRESS,
					    "Could not process the request because the PPP connection is already being started.");
      break;

    case NM_DIALUP_STATE_STARTED:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_ALREADY_STARTED,
					    "Could not process the request because a PPP connection was already active.");
      break;

    case NM_DIALUP_STATE_STOPPING:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_STOPPING_IN_PROGRESS,
					    "Could not process the request because the PPP connection is being stopped.");
      break;

    case NM_DIALUP_STATE_STOPPED:
      nm_ppp_cancel_quit_timer (data);
      nm_ppp_dbus_handle_start_pppd (message, data);
      reply = dbus_message_new_method_return (message);
      break;

    default:
      g_assert_not_reached();
      break;
    }

  return reply;
}


/*
 * nm_ppp_dbus_stop_ppp
 *
 * Terminate a PPP connection.
 *
 */
static DBusMessage *
nm_ppp_dbus_stop_pppd (DBusConnection *con, DBusMessage *message, NmPPPData *data)
{
  DBusMessage		*reply = NULL;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (con != NULL, NULL);
  g_return_val_if_fail (message != NULL, NULL);
  
  switch (data->state)
    {
    case NM_DIALUP_STATE_STOPPING:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_STOPPING_IN_PROGRESS,
					    "Could not process the request because the PPP connection is already being stopped.");
      break;

    case NM_DIALUP_STATE_STOPPED:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_ALREADY_STOPPED,
					    "Could not process the request because no PPP connection was active.");
      break;

    case NM_DIALUP_STATE_STARTING:
    case NM_DIALUP_STATE_STARTED:
      nm_warning("5");
      nm_ppp_dbus_handle_stop_pppd (data);
      reply = dbus_message_new_method_return (message);
      break;

    default:
      g_assert_not_reached();
      break;
    }

  return reply;
}


/*
 * nm_ppp_dbus_get_state
 *
 * Return some state information to NetworkManager.
 *
 */
static DBusMessage *
nm_ppp_dbus_get_state (DBusConnection *con, DBusMessage *message, NmPPPData *data)
{
  DBusMessage		*reply = NULL;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (con != NULL, NULL);
  g_return_val_if_fail (message != NULL, NULL);

  if ((reply = dbus_message_new_method_return (message)))
    dbus_message_append_args (reply, DBUS_TYPE_UINT32, &(data->state), DBUS_TYPE_INVALID);

  return reply;
}


/*
 * nm_ppp_dbus_process_helper_config_error
 *
 * Signal the bus that the helper could not get all the configuration information
 * it needed.
 *
 */
static void
nm_ppp_dbus_process_helper_config_error (DBusConnection *con, DBusMessage *message, NmPPPData *data)
{
  char *error_item;

  g_return_if_fail (data != NULL);
  g_return_if_fail (con != NULL);
  g_return_if_fail (message != NULL);

  /* Only accept the config info if we're in STARTING state */
  if (data->state != NM_DIALUP_STATE_STARTING)
    return;

  if (dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &error_item, DBUS_TYPE_INVALID))
    {
      nm_warning ("ppp helper did not receive adequate configuration information from ppp.  It is missing '%s'.", error_item);
      nm_ppp_dbus_signal_failure (data, NM_DBUS_DIALUP_SIGNAL_IP_CONFIG_BAD);
    }

  nm_ppp_cancel_helper_timer (data);
  nm_ppp_dbus_handle_stop_pppd (data);
}


/*
 * nm_ppp_dbus_process_helper_ip4_config
 *
 * Signal the bus 
 *
 */
static void
nm_ppp_dbus_process_helper_ip4_config (DBusConnection *con, DBusMessage *message, NmPPPData *data)
{
  char *                ifname;
  guint32		ip4_gateway;
  guint32		ip4_address;
  guint32		ip4_ptpaddr;
  guint32		ip4_netmask;
  guint32 		ip4_dns_1;
  guint32 		ip4_dns_2;
  guint32 *		ip4_dns;
  guint32		ip4_dns_len;
  guint32 		ip4_nbns_1;
  guint32 		ip4_nbns_2;
  guint32 *		ip4_nbns;
  guint32		ip4_nbns_len;
  gboolean		success = FALSE;
  guint32               ip4_zero = 0;

  g_return_if_fail (data != NULL);
  g_return_if_fail (con != NULL);
  g_return_if_fail (message != NULL);
  
  /* Only accept the config info if we're in STARTING state */
  if (data->state != NM_DIALUP_STATE_STARTING) {
    nm_warning ("IP4 config received while not in STARTING state");
    return;
  }

  nm_ppp_cancel_helper_timer (data);

  nm_info ("Processing ip4 config message");

  if (dbus_message_get_args(message, NULL,
			    DBUS_TYPE_STRING, &ifname,
			    DBUS_TYPE_UINT32, &ip4_address,
			    DBUS_TYPE_UINT32, &ip4_ptpaddr,
			    DBUS_TYPE_UINT32, &ip4_netmask,
			    DBUS_TYPE_UINT32, &ip4_dns_1,
			    DBUS_TYPE_UINT32, &ip4_dns_2,
			    DBUS_TYPE_UINT32, &ip4_nbns_1,
			    DBUS_TYPE_UINT32, &ip4_nbns_2,
			    DBUS_TYPE_INVALID)) {

    DBusMessage	*sig;

    if (!(sig = dbus_message_new_signal (NM_DBUS_PATH_PPP, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_SIGNAL_IP4_CONFIG))) {
      nm_warning ("Not enough memory for new dbus message!");
    } else {

      GArray *ip4_dns_buf = g_array_new(TRUE, TRUE, sizeof(guint32));
      GArray *ip4_nbns_buf = g_array_new(TRUE, TRUE, sizeof(guint32));

      guint32 *tmp;

      if ( ip4_dns_1 != 0 ) {
	tmp = &ip4_dns_1;
	g_array_append_val ( ip4_dns_buf, tmp );
      }
      if ( ip4_dns_2 != 0 ) {
	tmp = &ip4_dns_2;
	g_array_append_val ( ip4_dns_buf, tmp );
      }
      if ( ip4_dns_buf->len == 0 ) {
	// we have to have a dummy at least
	tmp = &ip4_zero;
	g_array_append_val ( ip4_dns_buf, tmp );
      }

      if ( ip4_nbns_1 != 0 ) {
	tmp = &ip4_nbns_1;
	g_array_append_val ( ip4_nbns_buf, tmp );
      }
      if ( ip4_nbns_2 != 0 ) {
	tmp = &ip4_nbns_2;
	g_array_append_val ( ip4_nbns_buf, tmp );
      }
      if ( ip4_nbns_buf->len == 0 ) {
	// we have to have a dummy at least
	tmp = &ip4_zero;
	g_array_append_val ( ip4_nbns_buf, tmp );
      }

      ip4_dns = (guint32 *)ip4_dns_buf->data;
      ip4_nbns = (guint32 *)ip4_nbns_buf->data;
      ip4_dns_len = ip4_dns_buf->len;
      ip4_nbns_len = ip4_nbns_buf->len;

      dbus_message_append_args (sig,
				DBUS_TYPE_STRING, &ifname,
				DBUS_TYPE_UINT32, &ip4_gateway,
				DBUS_TYPE_UINT32, &ip4_address,
				DBUS_TYPE_UINT32, &ip4_ptpaddr,
				DBUS_TYPE_UINT32, &ip4_netmask,
				DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &ip4_dns, ip4_dns_len,
				DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &ip4_nbns, ip4_nbns_len,
				DBUS_TYPE_INVALID);
      
      nm_warning("Sending IP configuration");
      if (!dbus_connection_send (data->con, sig, NULL)) {
	nm_warning ("Could not raise the "NM_DBUS_DIALUP_SIGNAL_IP4_CONFIG" signal!");
	goto out;
      }

      dbus_message_unref (sig);

      g_array_free( ip4_dns_buf, FALSE );
      g_array_free( ip4_nbns_buf, FALSE );

      nm_ppp_set_state (data, NM_DIALUP_STATE_STARTED);
      success = TRUE;
    }
  } else {
    nm_warning("Could not parse args for signalIP4Config");
  }

out:
  if (!success)
    {
      nm_warning ("Received invalid IP4 Config information from helper, terminating ppp.");
      nm_warning("7");
      nm_ppp_dbus_handle_stop_pppd (data);
    }
}


/*
 * nm_ppp_dbus_get_auth_info
 *
 * Pass authentication information to the PPPD plugin.
 *
 */
static DBusMessage *
nm_ppp_dbus_get_auth_info (DBusConnection *con, DBusMessage *message, NmPPPData *data)
{
  DBusMessage		*reply = NULL;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (con != NULL, NULL);
  g_return_val_if_fail (message != NULL, NULL);

  if (data->io_data == NULL) {
    nm_warning("Should never happen: program not started but it already requests config data");
    return NULL;
  }

  if ( (data->io_data->username == NULL) ||
       (data->io_data->password == NULL) ) {
    nm_warning("Should never happen: no user/pass credentials available");
    return NULL;
  }

  if ((reply = dbus_message_new_method_return (message))) {
    dbus_message_append_args (reply, 
			      DBUS_TYPE_STRING, &(data->io_data->username),
			      DBUS_TYPE_STRING, &(data->io_data->password),
			      DBUS_TYPE_INVALID);
  } else {
    nm_info("Build of getAuthInfo reply failed ");
  }

  return reply;
}



/*
 * nm_ppp_dbus_message_handler
 *
 * Handle requests for our services.
 *
 */
static DBusHandlerResult
nm_ppp_dbus_message_handler (DBusConnection *con, DBusMessage *message, void *user_data)
{
  NmPPPData		*data = (NmPPPData *)user_data;
  const char		*method;
  const char		*path;
  DBusMessage		*reply = NULL;
  gboolean			 handled = TRUE;

  g_return_val_if_fail (data != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
  g_return_val_if_fail (con != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
  g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

  method = dbus_message_get_member (message);
  path = dbus_message_get_path (message);

  nm_info ("nm_ppp_dbus_message_handler() got method '%s' for path '%s'.", method, path);

  /* If we aren't ready to accept dbus messages, don't */
  if ((data->state == NM_DIALUP_STATE_INIT) || (data->state == NM_DIALUP_STATE_SHUTDOWN))
    {
      nm_warning ("Received dbus messages but couldn't handle them due to INIT or SHUTDOWN states.");
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_WRONG_STATE,
					    "Could not process the request due to current state of STATE_INIT or STATE_SHUTDOWN.");
      goto reply;
    }

  if (strcmp ("startConnection", method) == 0)
    reply = nm_ppp_dbus_start_pppd (con, message, data);
  else if (strcmp ("stopConnection", method) == 0)
    reply = nm_ppp_dbus_stop_pppd (con, message, data);
  else if (strcmp ("getState", method) == 0)
    reply = nm_ppp_dbus_get_state (con, message, data);
  else if (strcmp ("signalConfigError", method) == 0)
    nm_ppp_dbus_process_helper_config_error (con, message, data);
  else if (strcmp ("signalIP4Config", method) == 0)
    nm_ppp_dbus_process_helper_ip4_config (con, message, data);
  else if (strcmp ("getAuthInfo", method) == 0)
    reply = nm_ppp_dbus_get_auth_info (con, message, data);
  else
    handled = FALSE;
  
 reply:
  if (reply)
    {
      dbus_connection_send (con, reply, NULL);
      dbus_message_unref (reply);
    }

  return (handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}


/*
 * nm_ppp_dbus_filter
 *
 * Handle signals from the bus, like NetworkManager network state
 * signals.
 *
 */
static DBusHandlerResult
nm_ppp_dbus_filter (DBusConnection *con, DBusMessage *message, void *user_data)
{
  NmPPPData	*data = (NmPPPData *)user_data;
  gboolean		handled = FALSE;
  DBusError		error;
  
  g_return_val_if_fail (data != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
  g_return_val_if_fail (con != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
  g_return_val_if_fail (message != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
  
  if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
    char 	*service;
    char		*old_owner;
    char		*new_owner;
    
    dbus_error_init (&error);
    if (    dbus_message_get_args (message, &error,
				   DBUS_TYPE_STRING, &service,
				   DBUS_TYPE_STRING, &old_owner,
				   DBUS_TYPE_STRING, &new_owner,
				   DBUS_TYPE_INVALID))
      {
	gboolean old_owner_good = (old_owner && (strlen (old_owner) > 0));
	gboolean new_owner_good = (new_owner && (strlen (new_owner) > 0));
	
	if ((!old_owner_good && new_owner_good) && (strcmp (service, NM_DBUS_SERVICE) == 0))	/* Equivalent to old ServiceCreated signal */
	  {
	  }
	else if ((old_owner_good && !new_owner_good) && (strcmp (service, NM_DBUS_SERVICE) == 0))	/* Equivalent to old ServiceDeleted signal */
	  {
	      /* If NM goes away, we don't stick around */
	    nm_warning("1");
	    nm_ppp_dbus_handle_stop_pppd (data);
	    g_main_loop_quit (data->loop);
	  }
      }
  } else if (dbus_message_is_signal (message, NM_DBUS_INTERFACE, "DeviceNoLongerActive"))
    {
      /* If the active device goes down our PPP is certainly not going to work. */
      nm_warning("2");
      nm_ppp_dbus_handle_stop_pppd (data);
    }
  
  return (handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}


/*
 * nm_ppp_dbus_init
 *
 * Grab our connection to the system bus, return NULL if anything goes wrong.
 *
 */
static DBusConnection *
nm_ppp_dbus_init (NmPPPData *data)
{
  DBusConnection			*connection = NULL;
  DBusError				 error;
  DBusObjectPathVTable	 vtable = { NULL, &nm_ppp_dbus_message_handler, NULL, NULL, NULL, NULL };

  g_return_val_if_fail (data != NULL, NULL);
  
  dbus_error_init (&error);
  if (!(connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error)))
    {
      nm_warning ("Error connecting to system bus: '%s'", error.message);
      goto out;
    }
  
  dbus_connection_setup_with_g_main (connection, NULL);

  dbus_error_init (&error);
  dbus_bus_request_name (connection, NM_DBUS_SERVICE_PPP, 0, &error);
  if (dbus_error_is_set (&error))
    {
      nm_warning ("Could not acquire the dbus service.  dbus_bus_request_name() says: '%s'", error.message);
      goto out;
    }
  
  if (!dbus_connection_register_object_path (connection, NM_DBUS_PATH_PPP, &vtable, data))
    {
      nm_warning ("Could not register a dbus handler for nm-ppp-service.  Not enough memory?");
      return NULL;
    }
  
  if (!dbus_connection_add_filter (connection, nm_ppp_dbus_filter, data, NULL))
    return NULL;

  dbus_error_init (&error);
  dbus_bus_add_match (connection,
		      "type='signal',"
		      "interface='" NM_DBUS_INTERFACE "',"
		      "sender='" NM_DBUS_SERVICE "',"
		      "path='" NM_DBUS_PATH "'",
		      &error);
  if (dbus_error_is_set (&error))
    goto out;

  dbus_bus_add_match (connection,
		      "type='signal',"
		      "interface='" DBUS_INTERFACE_DBUS "',"
		      "sender='" DBUS_SERVICE_DBUS "'",
		      &error);
  if (dbus_error_is_set (&error))
    goto out;
  
out:
  if (dbus_error_is_set (&error))
    {
      dbus_error_free (&error);
      connection = NULL;
    }
  return connection;
}



NmPPPData *ppp_data = NULL;

static void
sigterm_handler (int signum)
{
  nm_info ("nm-ppp-service caught SIGINT/SIGTERM");

  g_main_loop_quit (ppp_data->loop);
}


/*
 * main
 *
 */
int
main( int argc, char *argv[] )
{
  struct sigaction	action;
  sigset_t			block_mask;

  g_type_init ();
  if (!g_thread_supported ())
    g_thread_init (NULL);

  ppp_data = g_malloc0 (sizeof (NmPPPData));
  ppp_data->state = NM_DIALUP_STATE_INIT;
  ppp_data->loop = g_main_loop_new (NULL, FALSE);

  if (!(ppp_data->con = nm_ppp_dbus_init (ppp_data)))
    exit (EXIT_FAILURE);

  action.sa_handler = sigterm_handler;
  sigemptyset (&block_mask);
  action.sa_mask = block_mask;
  action.sa_flags = 0;
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);

  nm_ppp_set_state (ppp_data, NM_DIALUP_STATE_STOPPED);
  g_main_loop_run (ppp_data->loop);

  nm_ppp_dbus_handle_stop_pppd (ppp_data);

  g_main_loop_unref (ppp_data->loop);
  g_free (ppp_data);

  exit (EXIT_SUCCESS);
}
