#include <stdlib.h>

#include "gsystem-local-alloc.h"

/* FIXME: Things we probably shouldn't need. */
#include "nm-config.h"
#include "nm-linux-platform.h"

#include "nm-device-ethernet.h"

int
main (int argc, char **argv)
{
	GMainLoop *loop = g_main_loop_new (NULL, FALSE);
	gs_unref_object NMDevice *device;
	GArray *links = NULL;
	GError *error = NULL;
	int i;

	/* Read the config file and CLI overrides */
	nm_config_new (&error);
	g_assert (!error);
	nm_linux_platform_setup ();

	links = nm_platform_link_get_all ();
	for (i = 0; i < links->len; i++) {
		NMPlatformLink *item = &g_array_index (links, NMPlatformLink, i);

		if (item->type == NM_LINK_TYPE_ETHERNET) {
			device = nm_device_ethernet_new (item);
			break;
		}
	}
	g_array_unref (links);

	g_idle_add ((GSourceFunc) g_main_loop_quit, loop);
	g_main_loop_run (loop);

	return EXIT_SUCCESS;
}
