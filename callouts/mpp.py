#!/usr/bin/python
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# (C) Copyright 2007 One Laptop Per Child
#

import commands, sys, syslog, os

def get_ip4_address(iface):
    import socket
    import fcntl
    import struct
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    fd = s.fileno()
    SIOCGIFADDR = 0x8915
    addr = fcntl.ioctl(fd, SIOCGIFADDR, struct.pack('256s', iface[:15]))[20:24]
    s.close()
    return socket.inet_ntoa(addr)

def get_hw_address(iface):
    import struct
    import fcntl
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    fd = s.fileno()
    SIOCGIFHWADDR = 0x8927
    req = fcntl.ioctl(fd, SIOCGIFHWADDR, struct.pack('16s16s', iface, ''))
    s.close()
    ignore1, ignore2, addr_high, addr_low = struct.unpack('>16sHHL8x', req)
    addr = (long(addr_high)<<32) + addr_low
    b0 = addr & 0xFF
    b1 = (addr >> 8) & 0xFF
    b2 = (addr >> 16) & 0xFF
    b3 = (addr >> 24) & 0xFF
    b4 = (addr >> 32) & 0xFF
    b5 = (addr >> 40) & 0xFF
    return "%02X:%02X:%02X:%02X:%02X:%02X" % (b5, b4, b3, b2, b1, b0)

def set_anycast(mask, mesh_dev):
    commands.getstatusoutput('echo "%s" > /sys/class/net/%s/anycast_mask' % (mask, mesh_dev))

def ipt(args):
    (s, o) = commands.getstatusoutput("/sbin/iptables %s" % args)
    if (s != 0):
        syslog("Error executing iptables command '%s': %s" % (args, o))

def masq_start(extif, intif):
    os.system('echo "1" > /proc/sys/net/ipv4/ip_forward')
    os.system('echo "1" > /proc/sys/net/ipv4/ip_dynaddr')

    modules = ["ip_tables", "ip_conntrack", "ip_conntrack_ftp", "ip_conntrack_irc",
                "iptable_nat", "ip_nat_ftp", "ip_nat_irc"]

    os.system("/sbin/modprobe " + " ".join(modules))

    ipt("-P INPUT ACCEPT")
    ipt("-F INPUT")
    ipt("-P OUTPUT ACCEPT")
    ipt("-F OUTPUT")
    ipt("-P FORWARD DROP")
    ipt("-F FORWARD")
    ipt("-t nat -F")

    # FWD: Allow all connections OUT and only existing and related ones IN
    ipt("-A FORWARD -i %s -o %s -m state --state ESTABLISHED,RELATED -j ACCEPT" % (extif, intif))
    ipt("-A FORWARD -i %s -o %s -j ACCEPT" % (extif, intif))
    ipt("-A FORWARD -i %s -o %s -j ACCEPT" % (intif, extif))
    #ipt("-A FORWARD -j LOG")
    
    # Enabling SNAT (MASQUERADE) functionality on $EXTIF
    ipt("-t nat -A POSTROUTING -o %s -j MASQUERADE" % extif)

def masq_stop():
    ipt("-F INPUT")
    ipt("-F OUTPUT")
    ipt("-P FORWARD DROP")
    ipt("-F FORWARD")
    ipt("-F -t nat")

    # Delete all User-specified chains
    ipt("-X")

    # Reset all IPTABLES counters
    ipt("-Z")

def mpp_start(mesh_dev, primary_dev):
    dns_file = file('/etc/resolv.conf','r')
    dns_addresses = ""
    for line in dns_file.readlines():
        if len(line.split()) >= 2 and line.split()[0] == "nameserver":
            dns_addresses += line.split()[1] + ", "
    dns_addresses = dns_addresses[:len(dns_addresses) - 2]
    dns_file.close()

    mesh_ip4_addr = get_ip4_address(mesh_dev)
    if not mesh_ip4_addr or not len(mesh_ip4_addr):
        return

    primary_hw_addr = get_hw_address(primary_dev).lower()

    #copy parameters to the DHCP conf file
    dhcpd_conf_text = """#Custom DHCP daemon configuration file - XO as MPP
ddns-update-style interim;

# free the addresses quickly, clients ignore them anyway
default-lease-time      60;
max-lease-time          60;

option domain-name-servers %s;

# Ignore requests from ourselves.  Because the 8388's mesh interface has
# the same MAC address as the eth interface, dhclient gets confused
class "me" {
    match if hardware = 01:%s;
}

subnet 169.254.0.0 netmask 255.255.0.0 {
    pool {
        deny members of "me";
        option routers            %s;
        # range of link-local addresses, won't be used by XO's
        range 169.254.0.1  169.254.255.254;
    }
}
"""   % (dns_addresses, primary_hw_addr, mesh_ip4_addr)
    
    fd = open("/etc/dhcpd.conf","w")
    fd.write(dhcpd_conf_text)
    fd.flush()
    fd.close()
    
    masq_start(primary_dev, mesh_dev)
    
    # Start MPP functionality in mesh firmware
    set_anycast("0x2", mesh_dev)   # mask for xo-as-mpp
    
    # Tell dhcpd to only listen on the mesh interface
    fd = open("/etc/sysconfig/dhcpd", "w")
    fd.write('DHCPDARGS="%s"' % mesh_dev)
    fd.close()
    (s, o) = commands.getstatusoutput("service dhcpd restart")

def mpp_stop(mesh_dev, primary_dev):
    masq_stop()

    (s, o) = commands.getstatusoutput("service dhcpd stop")
    try:
        os.remove("/etc/sysconfig/dhcpd")
        os.remove("/etc/dhcpd.conf")
    except OSError, e:
        pass

    # Stop MPP functionality in mesh firmware
    set_anycast("0x0", mesh_dev)   # mask for off


def main():
    if len(sys.argv) < 4:
        sys.exit()

    mesh_dev = sys.argv[1]
    action = sys.argv[2]
    primary_dev = sys.argv[3]

    if action == "meshready":
        mpp_start(mesh_dev, primary_dev)
    elif action == "meshdown":
        mpp_stop(mesh_dev, primary_dev)    

if __name__ == "__main__":
    main()
