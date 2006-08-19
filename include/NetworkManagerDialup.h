/* NetworkManager -- Network link manager
 *
 * Tim Niemueller [www.niemueller.de]
 * Dan Williams <dcbw@redhat.com>
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

#ifndef NETWORK_MANAGER_DIALUP_H
#define NETWORK_MANAGER_DIALUP_H

/*
 * dbus services details
 */
#define	NM_DBUS_PATH_DIALUP			"/org/freedesktop/NetworkManager/DialupConnections"
#define	NM_DBUS_INTERFACE_DIALUP		"org.freedesktop.NetworkManager.DialupConnections"


/*
 * Dialup Errors
 */
#define NM_DBUS_NO_ACTIVE_DIALUP_CONNECTION	"org.freedesktop.NetworkManager.DialupConnections.NoActiveDialupConnection"
#define NM_DBUS_NO_DIALUP_CONNECTIONS			"org.freedesktop.NetworkManager.DialupConnections.NoDialupConnections"
#define NM_DBUS_INVALID_DIALUP_CONNECTION		"org.freedesktop.NetworkManager.DialupConnections.InvalidDialupConnection"

#define NM_DBUS_DIALUP_STARTING_IN_PROGRESS	"StartingInProgress"
#define NM_DBUS_DIALUP_ALREADY_STARTED		"AlreadyStarted"
#define NM_DBUS_DIALUP_STOPPING_IN_PROGRESS	"StoppingInProgress"
#define NM_DBUS_DIALUP_ALREADY_STOPPED		"AlreadyStopped"
#define NM_DBUS_DIALUP_WRONG_STATE			"WrongState"
#define NM_DBUS_DIALUP_BAD_ARGUMENTS			"BadArguments"


/*
 * Dialup signals
 */
#define NM_DBUS_DIALUP_SIGNAL_LOGIN_FAILED		"LoginFailed"
#define NM_DBUS_DIALUP_SIGNAL_LAUNCH_FAILED	"LaunchFailed"
#define NM_DBUS_DIALUP_SIGNAL_CONNECT_FAILED	"ConnectFailed"
#define NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD	"DialupConfigBad"
#define NM_DBUS_DIALUP_SIGNAL_IP_CONFIG_BAD	"IPConfigBad"
#define NM_DBUS_DIALUP_SIGNAL_STATE_CHANGE		"StateChange"
#define NM_DBUS_DIALUP_SIGNAL_IP4_CONFIG		"IP4Config"

/*
 * Dialup connection states
 */
typedef enum NMDialupState
{
	NM_DIALUP_STATE_UNKNOWN = 0,
	NM_DIALUP_STATE_INIT,
	NM_DIALUP_STATE_SHUTDOWN,
	NM_DIALUP_STATE_STARTING,
	NM_DIALUP_STATE_STARTED,
	NM_DIALUP_STATE_STOPPING,
	NM_DIALUP_STATE_STOPPED
} NMDialupState;


/*
 * Dialup connection activation stages
 */
typedef enum NMDialupActStage
{
	NM_DIALUP_ACT_STAGE_UNKNOWN = 0,
	NM_DIALUP_ACT_STAGE_DISCONNECTED,
	NM_DIALUP_ACT_STAGE_PREPARE,
	NM_DIALUP_ACT_STAGE_CONNECT,
	NM_DIALUP_ACT_STAGE_IP_CONFIG_GET,
	NM_DIALUP_ACT_STAGE_ACTIVATED,
	NM_DIALUP_ACT_STAGE_FAILED,
	NM_DIALUP_ACT_STAGE_CANCELED
} NMDialupActStage;


#endif /* NETWORK_MANAGER_DIALUP_H */
