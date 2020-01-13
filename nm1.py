#!/usr/bin/python3

import sys

import gi
gi.require_version("NM", "1.0")
from gi.repository import GLib, NM

class NmContext:

    @staticmethod
    def context_run(context, timeout_ms):
        mainloop = GLib.MainLoop.new(context, False)
        timeout_source = GLib.timeout_source_new(timeout_ms)
        timeout_source.set_callback(lambda x: mainloop.quit())
        timeout_source.attach(context)
        mainloop.run()
        timeout_source.destroy()

    @staticmethod
    def NM_IS_NEW_STYLE():
        # >= 1.21.3
        return NM.utils_version() >= 0x11503

    def __init__(self, create_async = False):
        if not create_async:
            client = NM.Client.new(None)
        else:
            if NmContext.NM_IS_NEW_STYLE():
                context = GLib.MainContext.get_thread_default()
            else:
                context = GLib.MainContext.default()
            mainloop = GLib.MainLoop.new(context, False)
            result_e_list = []
            def _async_cb(obj, async_result):
                try:
                    obj.init_finish(async_result)
                except Exception as e:
                    result_e_list.append(e)
                mainloop.quit()
            client = NM.Client()
            client.init_async(0, None, _async_cb)
            mainloop.run()
            if result_e_list:
                raise result_e_list[0]

        self.client = client

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback):
        if not self.client:
            return

        is_done = []

        if NmContext.NM_IS_NEW_STYLE():
            context = GLib.MainContext.default()
            is_done.append(1)
        else:
            context = self.client.get_main_context()
            self.client.get_context_busy_watcher().weak_ref(lambda: is_done.append(1))

        self.client = None

        while context.iteration(False):
            pass

        if not is_done:
            timeout_source = GLib.timeout_source_new(50)
            timeout_source.set_callback(lambda x: is_done.append(1))
            timeout_source.attach(context)
            while not is_done:
                context.iteration(True)
            timeout_source.destroy()

n_runs = int(sys.argv[1])
async_init = int(sys.argv[2]) != 0

last_timestamp = GLib.get_monotonic_time()
for i_run in range(0, n_runs):
    with NmContext(async_init) as ctx:
        print(f"{i_run:5}/{n_runs:5}: async-init={async_init}: Have {len(ctx.client.get_active_connections())} active connection")
    if False and (i_run % 50 == 49):
        print("wait for D-Bus timeout...")
        NmContext.context_run(GLib.MainContext.default(), 20*1000)
