#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# FOGLAMP_BEGIN
# See: http://foglamp.readthedocs.io/
# FOGLAMP_END

""" python35 plugin wrapper

$ python3 -m wrapper
"""


import ctypes
from ctypes import Structure, POINTER
from ctypes import c_char_p, c_int, c_void_p

import json

FILTER_LIB_PATH = '/usr/local/foglamp/plugins/filter/python35/'

""" make sure to set LD_LIBRARY_PATH if using sudo make install
$ export LD_LIBRARY_PATH=/usr/local/foglamp/lib 
"""

class PLUGIN_INFORMATION(Structure):
    """ 
    typedef struct {
        const char *name;
        const char *version;
        unsigned int options;
        const char *type;
        const char *interface;
	    const char *config;
    } PLUGIN_INFORMATION; 
    """

    _fields_= [
        ("name", c_char_p),
        ("version", c_char_p),
        ("options", c_int),
        ("type", c_char_p),
        ("interface", c_char_p),
        ("config", c_char_p)
    ]

    def __repr__(self):
        return '({0}, {1}, {2}, {3}, {4}, {5})'.format(self.name, self.version, self.options, self.type, self.interface, self.config)

try:
    _filter_module = ctypes.CDLL(FILTER_LIB_PATH + 'libpython35.so')

    def plugin_info():
        p_info = _filter_module.plugin_info
        p_info.restype =  ctypes.POINTER(PLUGIN_INFORMATION)
        p_inf = p_info().contents
        return p_inf
    
    pinf = plugin_info()
    print(pinf)
    print(pinf.name.decode())
    # print(pinf.version.decode())
    print(json.loads(pinf.config.decode()))

    
    def plugin_init():
        """  a wrapper method for

        PLUGIN_HANDLE plugin_init(ConfigCategory* config, OUTPUT_HANDLE *outHandle, OUTPUT_STREAM output)
        """
        p_init = _filter_module.plugin_init
        
        
        from ctypes import CFUNCTYPE, byref
    
        
        rset = [
            {'reading': {'power_set1': '5980'}, 'asset_code': 'lab1'},
            {'reading': {'power_set1': '211'}, 'asset_code': 'lab1'}
            ]

        # typedef void (*OUTPUT_STREAM)(OUTPUT_HANDLE *, READINGSET *);
        callback_type = CFUNCTYPE(c_void_p, c_void_p, c_void_p)

    
        p_init.argtypes = (c_char_p, c_void_p, callback_type)
        # return: typedef void * PLUGIN_HANDLE;
        p_init.restype = POINTER(c_void_p)    
    
        op_handle = POINTER(c_void_p)

        
        def os(op_handle, rset):
            print("py_cmp_func") 
            return 0 

        callback_func = callback_type(os) 
        
        try:
            # pinit = p_init(POINTER(pinf.config), POINTER(op_handle), callback)
            pinit = p_init(pinf.config, op_handle, byref(callback_func))
            print(pinit)
        except Exception as ex:
            import traceback
            print(traceback.format_exc())
        else:
            return op_handle

    pHandle = plugin_init()
    print(pHandle)

except Exception as ex:
    print(ex)
