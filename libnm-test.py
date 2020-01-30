#!/bin/python3

import sys
import uuid
import socket

import gi
gi.require_version('NM', '1.0')
from gi.repository import GLib, NM

mainloop = GLib.MainLoop.new(None, False)

PROFILE_NAME = 'x-libnm-test'

DEVICE_NAME = 'eth0'

def log(msg):
    NM.utils_log(f'{msg}')

def die(msg):
    log(f"DIE: {msg}")
    sys.exit(1)

def nmc_create():
    nmc = NM.Client()
    def cb(source, res):
        assert(source == nmc)
        try:
            source.init_finish(res)
        except Exception as e:
            die("failed to initialize NMClient: %s" % (e))
        mainloop.quit()
    nmc.init_async(0, None, cb)
    mainloop.run()
    return nmc

def create_profile(name):

    profile = NM.SimpleConnection.new()

    s_con = NM.SettingConnection.new()
    s_con.set_property(NM.SETTING_CONNECTION_ID, name)
    s_con.set_property(NM.SETTING_CONNECTION_UUID, str(uuid.uuid4()))
    s_con.set_property(NM.SETTING_CONNECTION_TYPE, "802-3-ethernet")

    s_wired = NM.SettingWired.new()

    s_ip4 = NM.SettingIP4Config.new()
    s_ip4.set_property(NM.SETTING_IP_CONFIG_METHOD, "manual")
    s_ip4.add_address(NM.IPAddress.new(socket.AF_INET, "192.168.33.5", 24))

    s_ip6 = NM.SettingIP6Config.new()
    s_ip6.set_property(NM.SETTING_IP_CONFIG_METHOD, "disabled")

    profile.add_setting(s_con)
    profile.add_setting(s_ip4)
    profile.add_setting(s_ip6)
    profile.add_setting(s_wired)

    return profile

def find_device(nmc, **kwargs):
    best = None
    for d in nmc.get_devices():
        if 'iface' in kwargs:
            if d.get_iface() != kwargs['iface']:
                continue
        if best is None:
            best = d
        elif kwargs.get('unique', True):
            raise Exception("Cannot find unique device for parameters %s" % (kwargs))
    return best

def add_and_activate(nmc, device, profile):
    res = {}
    def cb(source, result):
        assert(source == nmc)
        try:
            res['ac'] = source.add_and_activate_connection_finish(result);
            log("add and activate finished and got %s" % (res['ac'].get_path()))
            c = res['ac'].get_connection()
            log('    has %s' % (c))
            if not c:
                log("No connection when add-and-activate completes.");
                #res['ac'].handler_disconnect(100000)
        except Exception as e:
            log("add and activate finished and failed with %s" % (e))
            res['error'] = e
        mainloop.quit()
    log("add and activate '%s' (%s) starting..." % (profile.get_id(), profile.get_uuid()))
    nmc.add_and_activate_connection_async(profile,
                                          device,
                                          None,
                                          None,
                                          cb)
    mainloop.run()
    if 'error' in res:
        raise res['error']
    return res['ac']

nmc = nmc_create()

def cb(source, con):
    log(f'connection {con.get_id()}, {con.get_uuid()} added ({con.get_path()})')
nmc.connect('connection-added', cb)

device = find_device(nmc, iface = DEVICE_NAME)
assert(device)

profile = create_profile(PROFILE_NAME)

ac = add_and_activate(nmc, device, profile)
assert(ac)
con = ac.get_connection()

if not con:
    log('ac has no connection yet. Wait...')

    def cb(source, pspec):
        assert(source == ac)
        assert(pspec.name == 'connection')
        assert(ac.get_connection())
        log('event!')
        mainloop.quit()

    signal_id = ac.connect('notify::connection', cb)

    mainloop.run()

    ac.handler_disconnect(signal_id)
    con = ac.get_connection()
    if not con:
        log('ac has still no connection')
        sys.exit(1)

log('ac has connection %s (%s). Done...' % (con.get_id(), con.get_uuid()))
