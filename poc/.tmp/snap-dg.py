import os
os.environ.setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1")
os.environ.setdefault("WEBKIT_DISABLE_DMABUF_RENDERER", "1")
os.environ.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")

import sys
import gi
gi.require_version("Gtk", "3.0")
gi.require_version("WebKit2", "4.1")
from gi.repository import Gtk, WebKit2, GLib

url = sys.argv[1]
out = sys.argv[2]

win = Gtk.OffscreenWindow()
win.set_default_size(1280, 900)
view = WebKit2.WebView()
settings = view.get_settings()
settings.set_hardware_acceleration_policy(WebKit2.HardwareAccelerationPolicy.NEVER)
view.set_size_request(1280, 900)
win.add(view)

def on_load(web, event):
    def shoot():
        def done(webview, res, data):
            try:
                pix = webview.get_snapshot_finish(res)
                pix.write_to_png(out)
            finally:
                Gtk.main_quit()
        view.get_snapshot(
            WebKit2.SnapshotRegion.VISIBLE,
            WebKit2.SnapshotOptions.NONE,
            None, done, None)
        return False
    GLib.timeout_add(800, shoot)

view.connect("load-changed", lambda w, e: on_load(w, e) if e == WebKit2.LoadEvent.FINISHED else None)
win.show_all()
view.load_uri(url)
Gtk.main()
