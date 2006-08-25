/*
 * nm-dialup.c - dialup QA application
 *
 * Tim Niemueller <tim@niemueller.de>
 */

#define DBUS_API_SUBJECT_TO_CHANGE 1
#define PROGRESS_STEPS 15

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>

#include "NetworkManager.h"
#include "NetworkManagerDialup.h"
#include "../src/dialup/ppp/nm-ppp-service.h"

typedef struct 
{
	int value;
	double norm;
} Timeout;

static DBusHandlerResult
dbus_filter (DBusConnection *connection G_GNUC_UNUSED,
	     DBusMessage *message,
	     void *user_data G_GNUC_UNUSED)
{
  DBusError error;
  gboolean handled = FALSE;

  dbus_error_init (&error);

  if (dbus_message_is_signal (message, NM_DBUS_INTERFACE_PPP, NM_DBUS_DIALUP_SIGNAL_STATE_CHANGE)) {
    int old_state = -1;
    int new_state = -1;
    if (! dbus_message_get_args (message, &error,
				 DBUS_TYPE_UINT32, &old_state,
				 DBUS_TYPE_UINT32, &new_state,
				 DBUS_TYPE_INVALID ) ) {
      printf("State change signal: invalid message args\n");
    } else {
      printf("State change: %i -> %i\n", old_state, new_state);
    }
    handled = TRUE;
  } else if ( dbus_message_is_signal (message,
				      NM_DBUS_INTERFACE_PPP,
				      NM_DBUS_DIALUP_SIGNAL_LOGIN_FAILED)) {
    printf("Signal: login failed\n");
    handled = TRUE;
  } else if ( dbus_message_is_signal (message,
				      NM_DBUS_INTERFACE_PPP,
				      NM_DBUS_DIALUP_SIGNAL_LAUNCH_FAILED)) {
    printf("Signal: launch failed\n");
    handled = TRUE;
  } else if ( dbus_message_is_signal (message,
				      NM_DBUS_INTERFACE_PPP,
				      NM_DBUS_DIALUP_SIGNAL_CONNECT_FAILED)) {
    printf("Signal: connect failed\n");
    handled = TRUE;
  } else if ( dbus_message_is_signal (message,
				      NM_DBUS_INTERFACE_PPP,
				      NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD)) {
    printf("Signal: ppp config bad\n");
    handled = TRUE;
  }

  return (handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

static void
stop_connection (DBusConnection *connection)
{
  DBusMessage *message;
  DBusMessage *reply;
  DBusError error;

  message = dbus_message_new_method_call (NM_DBUS_SERVICE_PPP, NM_DBUS_PATH_PPP,
					  NM_DBUS_INTERFACE_PPP, "stopConnection");
  if (!message) {
    printf("Could not start connection\n");
    exit (2);
  }

  /*
  dbus_message_append_args (message,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &password_items, password_count,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &data_items, data_count,
			    DBUS_TYPE_INVALID);
  */

  dbus_error_init (&error);
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1, NULL);
  if (dbus_error_is_set (&error)) {
    printf ("stopConnection failed: %s raised %s\n", error.name, error.message);
  } else if ( !reply ) {
    printf("stopConnection failed: reply was NULL\n");
  } else {
    printf("Stopped connection\n");
  }
  dbus_message_unref (message);
  if (reply) dbus_message_unref(reply);
}


static void
start_connection (DBusConnection *connection)
{
  DBusMessage *message;
  DBusMessage *reply;
  DBusError error;

  
  GArray *password_items_buf = g_array_new(TRUE, TRUE, sizeof(gchar *));
  GArray *data_items_buf = g_array_new(TRUE, TRUE, sizeof(gchar *));

  int     password_count = -1;
  int     data_count = -1;

  char  **data_items = NULL;
  char  **password_items = NULL;

  char *str;
  str = g_strdup("t-mobile");
  g_array_append_val( password_items_buf, str );

  str = g_strdup("init_1");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("ATE1");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("init_2");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("AT+CGDCONT=1,\"IP\",\"internet.t-d1.de\",\"\",0,0");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("ttyname");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("/dev/rfcomm0");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("speed");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("57600");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("use-wvdial");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("yes");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("volume");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("0");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("flowcontrol");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("crtscts");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("number");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("*99#");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("username");
  g_array_append_val( data_items_buf, str );
  str = g_strdup("t-mobile");
  g_array_append_val( data_items_buf, str );

  password_count = password_items_buf->len;  
  password_items = (char **)(password_items_buf->data);

  data_count = data_items_buf->len;  
  data_items = (char **)(data_items_buf->data);

  message = dbus_message_new_method_call (NM_DBUS_SERVICE_PPP, NM_DBUS_PATH_PPP,
					  NM_DBUS_INTERFACE_PPP, "startConnection");
  if (!message) {
    printf("Could not start connection\n");
    exit (2);
  }

  dbus_message_append_args (message,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &password_items, password_count,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &data_items, data_count,
			    DBUS_TYPE_INVALID);


  dbus_error_init (&error);
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1, NULL);
  if (dbus_error_is_set (&error)) {
    printf ("startConnection failed: %s raised %s\n", error.name, error.message);
  } else if ( !reply ) {
    printf("startConnection failed: reply was NULL\n");
  } else {
    printf("Sent message\n");
  }
  dbus_message_unref (message);
  if (reply) dbus_message_unref(reply);

  g_array_free (password_items_buf, TRUE);
  g_array_free (data_items_buf, TRUE);
}


GMainLoop *loop;

void signal_handler(int);

void
signal_handler(int signum)
{
  if ( signum == SIGINT ) {
    g_main_loop_quit( loop );
  }
}


int
main (int argc, char *argv[])
{
  DBusConnection *connection;
  DBusError error;

  g_type_init ();

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
  if (connection == NULL) {
    dbus_error_free (&error);
    return 2;
  }

  dbus_connection_setup_with_g_main (connection, NULL);

  if ( ! dbus_connection_add_filter (connection, dbus_filter, NULL, NULL)) {
    return 2;
  }

  dbus_bus_add_match (connection,
		      "type='signal',"
		      "interface='" NM_DBUS_INTERFACE_PPP "',"
		      "sender='" NM_DBUS_SERVICE_PPP "',"
		      "path='" NM_DBUS_PATH_PPP "'", &error);
  if (dbus_error_is_set (&error)) {
    dbus_error_free (&error);
    return 2;
  }

  // order a connection start
  start_connection (connection);

  signal(SIGINT, signal_handler);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  stop_connection (connection);

  return 0;
}
