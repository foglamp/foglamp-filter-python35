"""
FogLAMP filtering for readings data
using Python 3.5

This filter scales each datapoint value in input readings data
"""

__author__ = "Massimiliano Pinto"
__copyright__ = "Copyright (c) 2019 Dianomic Systems"
__license__ = "Apache 2.0"
__version__ = "${VERSION}"

import sys
import json

"""
Filter configuration set by set_filter_config(config)
"""

"""
filter_config, global variable
"""
filter_config = dict()

"""
Set the Filter configuration into filter_config (global variable)

Input data is a dict with 'config' key and JSON string version wit data

JSON string is loaded into a dict, set to global variable filter_config

Return True
"""
def set_filter_config(configuration):
    #print(configuration)
    global filter_config
    filter_config = json.loads(configuration['config'])

    return True

"""
Method for filtering readings data

Input is array of dicts
[
    {'reading': {'power_set1': '5980'}, 'asset_code': 'lab1'},
    {'reading': {'power_set1': '211'}, 'asset_code': 'lab1'}
]

Input data:
   readings: can be modified, dropped etc
Output is array of dict
"""
def scale35(readings):
    # Default config values
    scale = 5
    offset = 10

    # Get configuration
    if ('scale' in filter_config):
        scale = filter_config['scale']

    if ('offset' in filter_config):
        offset = filter_config['offset']

    for elem in readings:
            #print("IN=" + str(elem))
            reading = elem['reading']

            # Apply some changes: multiply datapoint values by scale and add offset
            for key in reading:
                newVal = reading[key] * scale + offset
                reading[key] = newVal

            #print("OUT=" + str(elem))
    return readings
