#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <netlink/netlink.h>
#include <netlink/object.h>
#include <netlink/route/route.h>

static void
test_ip6_route_add (void)
{
	struct nl_sock *sock = nl_socket_alloc ();
	struct rtnl_route *rtnlroute;
	struct rtnl_nexthop *nexthop;
	struct nl_addr *dst, *gw;
	//int ifindex = 2;
	int prio = 1024;
	int tos = 0;
	int nle;

	rtnlroute = rtnl_route_alloc ();
	nexthop = rtnl_route_nh_alloc ();
	nl_addr_parse ("2001:db8:a:b::/64", AF_UNSPEC, &dst);
	//nl_addr_parse ("1:2:3:4:5:6:7:8", AF_UNSPEC, &gw);
	nl_addr_parse ("::", AF_UNSPEC, &gw);
	assert (rtnlroute && nexthop && dst);
	rtnl_route_set_table (rtnlroute, RT_TABLE_MAIN);
	rtnl_route_set_tos (rtnlroute, tos);
	rtnl_route_set_dst (rtnlroute, dst);
	rtnl_route_set_priority (rtnlroute, prio);
	//rtnl_route_nh_set_ifindex (nexthop, ifindex);
	rtnl_route_nh_set_gateway (nexthop, gw);
	rtnl_route_add_nexthop (rtnlroute, nexthop);
	assert (nl_object_identical ((struct nl_object *) rtnlroute, (struct nl_object *) rtnlroute));

	nl_connect (sock, NETLINK_ROUTE);

	nle = rtnl_route_add (sock, rtnlroute, 0);
	if (nle)
		fprintf (stderr, "Add: %s\n", nl_geterror (nle));
	system ("ip -6 route | grep 2001:db8");
	nle = rtnl_route_delete (sock, rtnlroute, 0);
	if (nle)
		fprintf (stderr, "Remove: %s\n", nl_geterror (nle));
}

int
main (int argc, char **argv)
{
	test_ip6_route_add ();

	return EXIT_SUCCESS;
}
