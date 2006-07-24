/* nm-ppp.h -- PPP connections
 *
 * Tim Niemueller [www.niemueller.de]
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
 * (C) Copyright 2006 Tim Niemueller
 */

#ifndef NETWORK_MANAGER_PPP_H
#define NETWORK_MANAGER_PPP_H

/*
 * PPP supervisor signals
 */
#define NM_DBUS_PPP_STARTING_IN_PROGRESS	"StartingInProgress"
#define NM_DBUS_PPP_ALREADY_STARTED		"AlreadyStarted"
#define NM_DBUS_PPP_STOPPING_IN_PROGRESS	"StoppingInProgress"
#define NM_DBUS_PPP_ALREADY_STOPPED		"AlreadyStopped"
#define NM_DBUS_PPP_WRONG_STATE			"WrongState"
#define NM_DBUS_PPP_BAD_ARGUMENTS			"BadArguments"

#define NM_DBUS_PPP_SIGNAL_LOGIN_FAILED		"LoginFailed"
#define NM_DBUS_PPP_SIGNAL_LAUNCH_FAILED	"LaunchFailed"
#define NM_DBUS_PPP_SIGNAL_CONNECT_FAILED	"ConnectFailed"
#define NM_DBUS_PPP_SIGNAL_PPP_CONFIG_BAD	"PPPConfigBad"
#define NM_DBUS_PPP_SIGNAL_IP_CONFIG_BAD	"IPConfigBad"
#define NM_DBUS_PPP_SIGNAL_STATE_CHANGE		"StateChange"
#define NM_DBUS_PPP_SIGNAL_IP4_CONFIG		"IP4Config"


/*
 * PPP connection states
 */
typedef enum NMPPPState
{
	NM_PPP_STATE_UNKNOWN = 0,
	NM_PPP_STATE_INIT,
	NM_PPP_STATE_SHUTDOWN,
	NM_PPP_STATE_STARTING,
	NM_PPP_STATE_STARTED,
	NM_PPP_STATE_STOPPING,
	NM_PPP_STATE_STOPPED
} NMPPPState;


/*
 * PPP connection activation stages
 */
typedef enum NMPPPActStage
{
	NM_PPP_ACT_STAGE_UNKNOWN = 0,
	NM_PPP_ACT_STAGE_DISCONNECTED,
	NM_PPP_ACT_STAGE_PREPARE,
	NM_PPP_ACT_STAGE_CONNECT,
	NM_PPP_ACT_STAGE_IP_CONFIG_GET,
	NM_PPP_ACT_STAGE_ACTIVATED,
	NM_PPP_ACT_STAGE_FAILED,
	NM_PPP_ACT_STAGE_CANCELED
} NMPPPActStage;

#endif
