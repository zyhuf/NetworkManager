/* NetworkManager audit support
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include <stdlib.h>
#include <syslog.h>
#include <linux/pkt_sched.h>

#include "platform/nm-linux-platform.h"
#include "platform/nmp-object.h"

#include "nm-test-utils-core.h"

NMTST_DEFINE ();

int
main (int argc, char **argv)
{
	GMainLoop *loop;
	NMPlatform *platform;
	int ifindex;
	NMPlatformQdisc qdisc1 = {
		.kind = "fq_codel",
		.addr_family = AF_UNSPEC,
		.handle = TC_H_MAKE (0x8003 << 16, 0),
		.parent = TC_H_ROOT,
		.info = 0,
	};
	NMPlatformQdisc qdisc2 = {
		.kind = "ingress",
		.addr_family = AF_UNSPEC,
		.handle = TC_H_MAKE (TC_H_INGRESS, 0),
		.parent = TC_H_INGRESS,
		.info = 0,
	};
	NMPlatformTfilter tfilter1 = {
		.kind = "matchall",
		.addr_family = AF_UNSPEC,
		.handle = TC_H_UNSPEC,
		.parent = TC_H_MAKE (0x8003 << 16, 0),
		.info = TC_H_MAKE (0, htons (ETH_P_ALL)),
		.action.kind = "simple",
		.action.simple.str = "Egress",
	};
	NMPlatformTfilter tfilter2 = {
		.kind = "matchall",
		.addr_family = AF_UNSPEC,
		.handle = TC_H_UNSPEC,
		.parent = TC_H_MAKE (TC_H_INGRESS, 0),
		.info = TC_H_MAKE (0, htons (ETH_P_ALL)),
		.action.kind = "simple",
		.action.simple.str = "Ingress",
	};

	if (!g_getenv ("NMTST_DEBUG"))
		g_setenv ("NMTST_DEBUG", "TRACE", TRUE);

	nmtst_init_with_logging (&argc, &argv, "DEBUG", "ALL");

	loop = g_main_loop_new (NULL, FALSE);

	nm_linux_platform_setup ();
	platform = NM_PLATFORM_GET;

	nm_platform_check_kernel_support (platform, ~((NMPlatformKernelSupportFlags) 0));

	ifindex = nm_platform_if_nametoindex (platform, "test0");
	if (ifindex > 0)
		nm_platform_link_delete (platform, ifindex);
	nm_platform_link_dummy_add (NM_PLATFORM_GET, "test0", NULL);
	ifindex = nm_platform_if_nametoindex (platform, "test0");

	qdisc1.ifindex = ifindex;
	g_assert (nm_platform_qdisc_add (platform, NMP_NLM_FLAG_ADD, &qdisc1) == NM_PLATFORM_ERROR_SUCCESS);

	qdisc2.ifindex = ifindex;
	g_assert (nm_platform_qdisc_add (platform, NMP_NLM_FLAG_ADD, &qdisc2) == NM_PLATFORM_ERROR_SUCCESS);

	tfilter1.ifindex = ifindex;
	g_assert (nm_platform_tfilter_add (NM_PLATFORM_GET, NMP_NLM_FLAG_ADD, &tfilter1) == NM_PLATFORM_ERROR_SUCCESS);

	tfilter2.ifindex = ifindex;
	g_assert (nm_platform_tfilter_add (NM_PLATFORM_GET, NMP_NLM_FLAG_ADD, &tfilter2) == NM_PLATFORM_ERROR_SUCCESS);

	// Egress:
	// tc -s filter ls dev test0
	// Ingress:
	// tc -s filter ls dev test0 parent ffff:

	g_main_loop_unref (loop);

	return EXIT_SUCCESS;
}
