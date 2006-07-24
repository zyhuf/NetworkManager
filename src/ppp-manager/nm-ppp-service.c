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
  NMPPPState		state;
  gboolean              use_wvdial;
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

  /*
  if ( strcmp (signal, NM_DBUS_VPN_SIGNAL_LOGIN_FAILED) == 0 )
    error_msg = _("The VPN login failed because the user name and password were not accepted or the certificate password was wrong.");
  else if (strcmp (signal, NM_DBUS_VPN_SIGNAL_LAUNCH_FAILED) == 0 )
    error_msg = _("The VPN login failed because the VPN program could not be started.");
  else if (strcmp (signal, NM_DBUS_VPN_SIGNAL_CONNECT_FAILED) == 0 )
    error_msg = _("The VPN login failed because the VPN program could not connect to the VPN server.");
  else if (strcmp (signal, NM_DBUS_VPN_SIGNAL_VPN_CONFIG_BAD) == 0 )
    error_msg = _("The VPN login failed because the VPN configuration options were invalid.");
  else if (strcmp (signal, NM_DBUS_VPN_SIGNAL_IP_CONFIG_BAD) == 0 )
    error_msg = _("The VPN login failed because the VPN program received an invalid configuration from the VPN server.");
  else
  */
    error_msg = _("PPP connection failed");

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
nm_ppp_dbus_signal_state_change (NmPPPData *data, NMPPPState old_state)
{
  DBusMessage	*message;

  g_return_if_fail (data != NULL);

  if (!(message = dbus_message_new_signal (NM_DBUS_PATH_PPP, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_SIGNAL_STATE_CHANGE)))
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
nm_ppp_set_state (NmPPPData *data, NMPPPState new_state)
{
  NMPPPState	old_state;

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

  nm_ppp_dbus_signal_failure (data, NM_DBUS_PPP_SIGNAL_CONNECT_FAILED);
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
      nm_ppp_dbus_signal_failure (data, NM_DBUS_PPP_SIGNAL_LOGIN_FAILED);
      break;

    case 1:	/* Other error (couldn't bind to address, etc) */
      nm_ppp_dbus_signal_failure (data, NM_DBUS_PPP_SIGNAL_CONNECT_FAILED);
      break;

    default:
      break;
    }

  nm_ppp_set_state (data, NM_PPP_STATE_STOPPED);
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
  printf("Writing config: %s\n", string);
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
				 char **passwords, const int num_passwords
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

  char         *username = NULL;
  char         *speed = NULL;
  char         *tty = NULL;
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

  g_return_val_if_fail (data != NULL, -1);

  data->pid = 0;

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
    if ( (strcmp( data_items[i], "use-wvdial" ) == 0) &&
		(strcmp( data_items[++i], "yes" ) == 0) ) {
      data->use_wvdial = TRUE;
    } else if ( strcmp( data_items[i], "speed" ) == 0) {
      speed = data_items[++i];
    } else if ( strcmp( data_items[i], "number" ) == 0) {
      number = data_items[++i];
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
    } else if ( strcmp( data_items[i], "ttyname" ) == 0) {
      tty = data_items[++i];
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
    }


    
    /*
    // Device, either tun or tap
    g_ptr_array_add (ppp_argv, (gpointer) "--dev");
    if ( (dev != NULL) ) {
      g_ptr_array_add (ppp_argv, (gpointer) dev);
    } else {
      // Versions prior to 0.3.0 didn't set this so we default for
      // tun for these configs
      g_ptr_array_add (ppp_argv, (gpointer) "tun");
    }
    */    
  }

  if ( data->use_wvdial ) {
    GPtrArray    *wvdial_argv = NULL;

    // Find wvdial, it's always needed
    const char *wvdial_binary = nm_find_wvdial();
    if (!wvdial_binary) {
      nm_info ("Could not find wvdial binary.");
      return -1;
    }

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


      write_config_option (stdin_fd, "[Dialer Default]\n");
      write_config_option (stdin_fd, "Modem = %s\n", tty);
      write_config_option (stdin_fd, "Baud = %s\n", speed);
      write_config_option (stdin_fd, "SetVolume = %s\n", volume);
      write_config_option (stdin_fd, "Dial Command = %s\n", "ATDT");
      write_config_option (stdin_fd, "Username = %s\n", username);
      write_config_option (stdin_fd, "Password = %s\n", passwords[0]);
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


      g_string_free( fc, TRUE );
      g_ptr_array_free (wvdial_argv, TRUE);
    }
  } else {
    GPtrArray    *ppp_argv;

    // Create pppd command line

    ppp_argv = g_ptr_array_new ();
    g_ptr_array_add (ppp_argv, (gpointer) (ppp_binary));
    g_ptr_array_add (ppp_argv, NULL);
    
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
    io_data->username        = g_strdup(username);
    io_data->password        = g_strdup(passwords[0]);
  
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
    { "use-wvdial",			OPT_TYPE_ASCII },
    { "speed",				OPT_TYPE_INTEGER },
    { "lock",				OPT_TYPE_ASCII },
    { "modem",				OPT_TYPE_ASCII },
    { "crtscts",			OPT_TYPE_ASCII },
    { "asyncmap",			OPT_TYPE_ASCII },
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
  char **		password_items = NULL;
  int		num_passwords = -1;
  const char *	name = NULL;
  const char *	user_name = NULL;
  DBusError		error;
  gboolean		success = FALSE;
  gint			ppp_fd = -1;	

  g_return_val_if_fail (message != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  nm_ppp_set_state (data, NM_PPP_STATE_STARTING);

  dbus_error_init (&error);
  if (!dbus_message_get_args (message, &error,
			      DBUS_TYPE_STRING, &name,
			      DBUS_TYPE_STRING, &user_name,
			      DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &password_items, &num_passwords,
			      DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &data_items, &num_items,
			      DBUS_TYPE_INVALID))
    {
      nm_warning ("Could not process the request because its arguments were invalid.  dbus said: '%s'", error.message);
      nm_ppp_dbus_signal_failure (data, NM_DBUS_PPP_SIGNAL_PPP_CONFIG_BAD);
      dbus_error_free (&error);
      goto out;
    }

  if (!nm_ppp_config_options_validate (data_items, num_items))
    {
      nm_ppp_dbus_signal_failure (data, NM_DBUS_PPP_SIGNAL_PPP_CONFIG_BAD);
      goto out;
    }

  /* Now we can finally try to activate the PPP */  if ((ppp_fd = nm_ppp_start_pppd_binary (data, data_items, num_items, password_items, num_passwords)) >= 0) {
    // Everything ok
    success = TRUE;
  }

out:
  dbus_free_string_array (data_items);
  dbus_free_string_array (password_items);
  if (!success)
    nm_ppp_set_state (data, NM_PPP_STATE_STOPPED);
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
      nm_ppp_set_state (data, NM_PPP_STATE_STOPPING);

      kill (data->pid, SIGINT);
      nm_info ("Terminated ppp daemon with PID %d.", data->pid);
      data->pid = 0;

      nm_ppp_set_state (data, NM_PPP_STATE_STOPPED);
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
    case NM_PPP_STATE_STARTING:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_STARTING_IN_PROGRESS,
					    "Could not process the request because the PPP connection is already being started.");
      break;

    case NM_PPP_STATE_STARTED:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_ALREADY_STARTED,
					    "Could not process the request because a PPP connection was already active.");
      break;

    case NM_PPP_STATE_STOPPING:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_STOPPING_IN_PROGRESS,
					    "Could not process the request because the PPP connection is being stopped.");
      break;

    case NM_PPP_STATE_STOPPED:
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
    case NM_PPP_STATE_STOPPING:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_STOPPING_IN_PROGRESS,
					    "Could not process the request because the PPP connection is already being stopped.");
      break;

    case NM_PPP_STATE_STOPPED:
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_ALREADY_STOPPED,
					    "Could not process the request because no PPP connection was active.");
      break;

    case NM_PPP_STATE_STARTING:
    case NM_PPP_STATE_STARTED:
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
  if (data->state != NM_PPP_STATE_STARTING)
    return;

  if (dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &error_item, DBUS_TYPE_INVALID))
    {
      nm_warning ("ppp helper did not receive adequate configuration information from ppp.  It is missing '%s'.", error_item);
      nm_ppp_dbus_signal_failure (data, NM_DBUS_PPP_SIGNAL_IP_CONFIG_BAD);
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
  guint32 *		ip4_dns;
  guint32		ip4_dns_len;
  guint32 *		ip4_nbns;
  guint32		ip4_nbns_len;
  gboolean		success = FALSE;

  g_return_if_fail (data != NULL);
  g_return_if_fail (con != NULL);
  g_return_if_fail (message != NULL);
  
  /* Only accept the config info if we're in STARTING state */
  if (data->state != NM_PPP_STATE_STARTING)
    return;

  nm_ppp_cancel_helper_timer (data);

  if (dbus_message_get_args(message, NULL,
			    DBUS_TYPE_UINT32, &ifname,
			    DBUS_TYPE_UINT32, &ip4_gateway,
			    DBUS_TYPE_UINT32, &ip4_address,
			    DBUS_TYPE_UINT32, &ip4_ptpaddr,
			    DBUS_TYPE_UINT32, &ip4_netmask,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &ip4_dns, &ip4_dns_len,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &ip4_nbns, &ip4_nbns_len,
			    DBUS_TYPE_INVALID))
    {
      DBusMessage	*sig;

      if (!(sig = dbus_message_new_signal (NM_DBUS_PATH_PPP, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_SIGNAL_IP4_CONFIG)))
	{
	  nm_warning ("Not enough memory for new dbus message!");
	  goto out;
	}

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
      if (!dbus_connection_send (data->con, sig, NULL))
	{
	  nm_warning ("Could not raise the "NM_DBUS_PPP_SIGNAL_IP4_CONFIG" signal!");
	  goto out;
	}

      dbus_message_unref (sig);
      nm_ppp_set_state (data, NM_PPP_STATE_STARTED);
      success = TRUE;
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

  if ((reply = dbus_message_new_method_return (message)))
    dbus_message_append_args (reply, 
			      DBUS_TYPE_STRING, &(data->io_data->username),
			      DBUS_TYPE_STRING, &(data->io_data->password),
			      DBUS_TYPE_INVALID);
  if (!reply)
    nm_info("Build of getAuthInfo reply failed ");

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

  /* nm_info ("nm_ppp_dbus_message_handler() got method '%s' for path '%s'.", method, path); */

  /* If we aren't ready to accept dbus messages, don't */
  if ((data->state == NM_PPP_STATE_INIT) || (data->state == NM_PPP_STATE_SHUTDOWN))
    {
      nm_warning ("Received dbus messages but couldn't handle them due to INIT or SHUTDOWN states.");
      reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_PPP_WRONG_STATE,
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
    nm_ppp_dbus_get_auth_info (con, message, data);
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
  ppp_data->state = NM_PPP_STATE_INIT;
  ppp_data->loop = g_main_loop_new (NULL, FALSE);

  if (!(ppp_data->con = nm_ppp_dbus_init (ppp_data)))
    exit (EXIT_FAILURE);

  action.sa_handler = sigterm_handler;
  sigemptyset (&block_mask);
  action.sa_mask = block_mask;
  action.sa_flags = 0;
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);

  nm_ppp_set_state (ppp_data, NM_PPP_STATE_STOPPED);
  g_main_loop_run (ppp_data->loop);

  nm_ppp_dbus_handle_stop_pppd (ppp_data);

  g_main_loop_unref (ppp_data->loop);
  g_free (ppp_data);

  exit (EXIT_SUCCESS);
}
