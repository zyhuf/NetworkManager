#include <stdlib.h>
#include <assert.h>
#include <netlink/netlink.h>
#include <netlink/object.h>
#include <netlink/route/route.h>

static void
test_ip4_route_identity (void)
{
	struct rtnl_route *rtnlroute1, *rtnlroute2;
	struct rtnl_nexthop *nexthop1, *nexthop2;
	struct nl_addr *dst1, *dst2, *gw1, *gw2;
	int prio = 1024;
	int tos = 0;

	rtnlroute1 = rtnl_route_alloc ();
	nexthop1 = rtnl_route_nh_alloc ();
	nl_addr_parse ("192.0.2.0/24", AF_UNSPEC, &dst1);
	nl_addr_parse ("198.51.100.0", AF_UNSPEC, &gw1);
	assert (rtnlroute1 && nexthop1 && dst1 && gw1);
	rtnl_route_set_table (rtnlroute1, RT_TABLE_MAIN);
	rtnl_route_set_tos (rtnlroute1, tos);
	rtnl_route_set_dst (rtnlroute1, dst1);
	rtnl_route_set_priority (rtnlroute1, prio);
	rtnl_route_nh_set_ifindex (nexthop1, 111);
	rtnl_route_nh_set_gateway (nexthop1, gw1);
	rtnl_route_add_nexthop (rtnlroute1, nexthop1);
	assert (nl_object_identical ((struct nl_object *) rtnlroute1, (struct nl_object *) rtnlroute1));

	rtnlroute2 = rtnl_route_alloc ();
	nexthop2 = rtnl_route_nh_alloc ();
	nl_addr_parse ("192.0.2.0/24", AF_UNSPEC, &dst2);
	nl_addr_parse ("1.2.3.4", AF_UNSPEC, &gw2);
	assert (rtnlroute2 && nexthop2 && dst2 && gw2);
	rtnl_route_set_table (rtnlroute2, RT_TABLE_MAIN);
	rtnl_route_set_tos (rtnlroute2, tos);
	rtnl_route_set_dst (rtnlroute2, dst2);
	rtnl_route_set_priority (rtnlroute2, prio);
	rtnl_route_nh_set_ifindex (nexthop2, 222);
	rtnl_route_nh_set_gateway (nexthop2, gw2);
	rtnl_route_add_nexthop (rtnlroute2, nexthop2);
	assert (nl_object_identical ((struct nl_object *) rtnlroute2, (struct nl_object *) rtnlroute2));

	assert (nl_object_identical ((struct nl_object *) rtnlroute1, (struct nl_object *) rtnlroute2));
}

static void
test_ip6_route_identity (void)
{
	struct rtnl_route *rtnlroute1, *rtnlroute2;
	struct rtnl_nexthop *nexthop1, *nexthop2;
	struct nl_addr *dst1, *dst2, *gw1, *gw2;
	int prio = 1024;
	int tos = 0;

	rtnlroute1 = rtnl_route_alloc ();
	nexthop1 = rtnl_route_nh_alloc ();
	nl_addr_parse ("2001:db8:a:b::/64", AF_UNSPEC, &dst1);
	nl_addr_parse ("2001:db8:e:f:a:b:c:d", AF_UNSPEC, &gw1);
	assert (rtnlroute1 && nexthop1 && dst1 && gw1);
	rtnl_route_set_table (rtnlroute1, RT_TABLE_MAIN);
	rtnl_route_set_tos (rtnlroute1, tos);
	rtnl_route_set_dst (rtnlroute1, dst1);
	rtnl_route_set_priority (rtnlroute1, prio);
	rtnl_route_nh_set_ifindex (nexthop1, 111);
	rtnl_route_nh_set_gateway (nexthop1, gw1);
	rtnl_route_add_nexthop (rtnlroute1, nexthop1);
	assert (nl_object_identical ((struct nl_object *) rtnlroute1, (struct nl_object *) rtnlroute1));

	rtnlroute2 = rtnl_route_alloc ();
	nexthop2 = rtnl_route_nh_alloc ();
	nl_addr_parse ("2001:db8:a:b::/64", AF_UNSPEC, &dst2);
	nl_addr_parse ("1:2:3:4:5:6:7:8", AF_UNSPEC, &gw2);
	assert (rtnlroute2 && nexthop2 && dst2 && gw2);
	rtnl_route_set_table (rtnlroute2, RT_TABLE_MAIN);
	rtnl_route_set_tos (rtnlroute2, tos);
	rtnl_route_set_dst (rtnlroute2, dst2);
	rtnl_route_set_priority (rtnlroute2, prio);
	rtnl_route_nh_set_ifindex (nexthop2, 222);
	rtnl_route_nh_set_gateway (nexthop2, gw2);
	rtnl_route_add_nexthop (rtnlroute2, nexthop2);
	assert (nl_object_identical ((struct nl_object *) rtnlroute2, (struct nl_object *) rtnlroute2));

	assert (nl_object_identical ((struct nl_object *) rtnlroute1, (struct nl_object *) rtnlroute2));
}

int
main (int argc, char **argv)
{
	test_ip4_route_identity ();
	test_ip6_route_identity ();

	return EXIT_SUCCESS;
}
