#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <netlink/netlink.h>
#include <netlink/object.h>
#include <netlink/route/addr.h>

int
main (int argc, char **argv)
{
	struct nl_sock *sock = nl_socket_alloc ();
	struct nl_cache *cache;
	struct rtnl_addr *rtnladdr = rtnl_addr_alloc ();
	int nle;

	nl_connect (sock, NETLINK_ROUTE);
	nle = rtnl_addr_alloc_cache (sock, &cache);
	assert (!nle);

	rtnl_addr_set_ifindex (rtnladdr, 5);
	nle = nl_cache_add (cache, (struct nl_object *) rtnladdr);
	assert (!nle);
	rtnl_addr_put (rtnladdr);

	nl_cache_free (cache);
	nl_socket_free (sock);

	return EXIT_SUCCESS;
}
