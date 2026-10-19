import ctypes
import sys

xcb = ctypes.CDLL('/workspace/sysroot/usr/lib/x86_64-linux-gnu/libxcb.so.1')
xcb.xcb_connect.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
xcb.xcb_connect.restype = ctypes.c_void_p
xcb.xcb_connection_has_error.argtypes = [ctypes.c_void_p]
xcb.xcb_disconnect.argtypes = [ctypes.c_void_p]
connection = xcb.xcb_connect(b':99', None)
failed = xcb.xcb_connection_has_error(connection)
xcb.xcb_disconnect(connection)
sys.exit(1 if failed else 0)
