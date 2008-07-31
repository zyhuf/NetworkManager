/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Probe modem capabilities
 *
 * Copyright (c) 2008 Vitja Makarov, <vitja.makarov@gmail.com>
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
 */

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nm-utils.h"
#include "probe-modem.h"


#define MODEM_CAP_GSM         0x0001 /* GSM commands */
#define MODEM_CAP_IS707_A     0x0002 /* CDMA circuit switched data commands */
#define MODEM_CAP_DS          0x0004 /* data compression */
#define MODEM_CAP_ES          0x0008 /* error control */
#define MODEM_CAP_FCLASS      0x0010 /* Fax commands */
#define MODEM_CAP_MS          0x0020 /* Modulation control commands */
#define MODEM_CAP_W           0x0040 /* Wireless commands */      

struct modem_caps {
	char *name;
	int bits;
};

static struct modem_caps modem_caps[] = {
	{"+CGSM",     MODEM_CAP_GSM},
	/* TODO: are they the same? */
	{"+CIS707-A", MODEM_CAP_IS707_A},
	{"+CIS707",   MODEM_CAP_IS707_A},
	{"+CIS707P",   MODEM_CAP_IS707_A},
	{NULL}
} ;

#define AT_CAPS_PROBE "AT+GCAP\r\n"

static int modem_probe_caps(int fd)
{
	char buf[200];
	char *ptr, *field = NULL;
	int err, ret = 0;

	err = write(fd, AT_CAPS_PROBE, sizeof(AT_CAPS_PROBE) - 1);
    
	if (err != sizeof(AT_CAPS_PROBE) - 1)
		return -1;

	/* 100ms is enough for modem to send all the data */
	usleep(100000);

	err = read(fd, buf, sizeof(buf) - 1);
	if (err <= 0)
		return -1;
	buf[err] = 0;

	/* check okay reply */
	ptr = strstr(buf, "\r\nOK\r\n");
	if (!ptr)
		return -1;
	*ptr = 0;

	/* find +GCAP: string */
	ptr = strstr(buf, "\r\n+GCAP:");
    
	if (ptr == NULL)
		return -1;
	ptr += 8;

	/* and parse it */
	do {
		err = *ptr == '\0' || *ptr == '\r' || *ptr == '\n';
		if (*ptr == ' ' || *ptr == ',' || err) {
			*ptr = 0;
			if (field) {
				struct modem_caps *cap = modem_caps;

				while (cap->name) {
					if (!strcmp(cap->name, field))
						ret |= cap->bits;
					cap++;
				}
			}
			field = NULL;
		} else if (NULL == field) {
			field = ptr;
		}
		ptr++;
	} while (!err);

	return ret;
}

int probe_modem (const char* device, const char *udi, LibHalContext *ctx)
{
	struct termios orig, attrs;
	int fd, caps;

	fd = open(device, O_RDWR|O_NDELAY);

	if (-1 == fd) {
		nm_error("open(%s): %s", device, strerror(errno));
		return -1;
	}

	if (tcgetattr(fd, &orig))
		return -1;
	
	memcpy(&attrs, &orig, sizeof(attrs));
	attrs.c_iflag &= ~(IGNCR | ICRNL | IUCLC | INPCK | IXON | IXANY | IGNPAR);
	attrs.c_oflag &= ~(OPOST | OLCUC | OCRNL | ONLCR | ONLRET);
	attrs.c_lflag &= ~(ICANON | XCASE | ECHO | ECHOE | ECHONL);
	attrs.c_lflag &= ~(ECHO | ECHOE);
	attrs.c_cc[VMIN] = 1;
	attrs.c_cc[VTIME] = 0;
	attrs.c_cc[VEOF] = 1;
	
	tcsetattr(fd, TCSANOW, &attrs);
	caps = modem_probe_caps(fd);
	tcsetattr(fd, TCSANOW, &orig);

	if (caps < 0) {
		nm_debug("Couldn't get caps");
		return -1;
	}
	
	if (caps & MODEM_CAP_GSM) {
		nm_debug("Found GSM modem");
		libhal_device_property_strlist_append(ctx, udi, "modem.command_sets", "GSM-07.07", NULL);
		libhal_device_property_strlist_append(ctx, udi, "modem.command_sets", "GSM-07.05", NULL);
		return 1;
	}
	
	if (caps & MODEM_CAP_IS707_A) {
		nm_debug("Found CDMA modem");
		libhal_device_property_strlist_append(ctx, udi, "modem.command_sets", "IS-707-A", NULL);
		return 1;
	}
	
	return 0;
}
