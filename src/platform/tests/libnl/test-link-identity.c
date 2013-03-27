#include <stdlib.h>
#include <assert.h>
#include <netlink/netlink.h>
#include <netlink/object.h>
#include <netlink/route/link.h>

int
main (int argc, char **argv)
{
	struct rtnl_link *rtnllink;
	int ifindex = 42;

	rtnllink = rtnl_link_alloc ();
	assert (rtnllink);
	rtnl_link_set_ifindex (rtnllink, ifindex);
	rtnl_link_set_family (rtnllink, AF_UNSPEC);
	assert (nl_object_identical ((struct nl_object *) rtnllink, (struct nl_object *) rtnllink));

	return EXIT_SUCCESS;
}
