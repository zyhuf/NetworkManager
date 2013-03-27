#include <stdlib.h>
#include <assert.h>
#include <netlink/netlink.h>
#include <netlink/object.h>
#include <netlink/route/addr.h>

int
main (int argc, char **argv)
{
	struct rtnl_addr *rtnladdr1, *rtnladdr2;
	struct nl_addr *nladdr1, *nladdr2;

	rtnladdr1 = rtnl_addr_alloc ();
	rtnl_addr_set_ifindex (rtnladdr1, 42);
	nl_addr_parse ("192.0.2.1/24", AF_UNSPEC, &nladdr1);
	rtnl_addr_set_local (rtnladdr1, nladdr1);

	rtnladdr2 = rtnl_addr_alloc ();
	rtnl_addr_set_ifindex (rtnladdr2, 42);
	nl_addr_parse ("192.0.2.1", AF_UNSPEC, &nladdr2);
	rtnl_addr_set_local (rtnladdr2, nladdr2);
	rtnl_addr_set_prefixlen (rtnladdr2, 24);

	assert (nl_object_identical ((struct nl_object *) rtnladdr1, (struct nl_object *) rtnladdr2));

	return EXIT_SUCCESS;
}
