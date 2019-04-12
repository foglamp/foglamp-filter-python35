/*
 * FogLAMP "Python 3.5" filter plugin.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#define FILTER_NAME "python35"

#include <utils.h>
#include <plugin_api.h>
#include <config_category.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <iostream>
#include <filter_plugin.h>
#include <filter.h>
#include <version.h>


#include "python35.h"

bool pythonInitialised = false;

/**
 * The Python 3.5 script module to load is set in
 * 'script' config item and it doesn't need the trailing .py
 *
 * Example:
 * if filename is 'readings_filter.py', just set 'readings_filter'
 * via FogLAMP configuration managewr
 *
 * Note:
 * Python 3.5 filter code needs two methods.
 *
 * One is the filtering method to call which must have
 * the same as the script name: it can not be changed.
 * The second one is the configuration entry point
 * method 'set_filter_config': it can not be changed
 *
 * Example: readings_filter.py
 *
 * expected two methods:
 * - set_filter_config(configuration) // Input is a string
 *   It sets the configuration internally as dict
 *
 * - readings_filter(readings) // Input is a dict
 *   It returns a dict with filtered input data
 */

// Filter default configuration
#define DEFAULT_CONFIG "{\"plugin\" : { \"description\" : \"Python 3.5 filter plugin\", " \
                       		"\"type\" : \"string\", " \
				"\"readonly\": \"true\", " \
				"\"default\" : \"" FILTER_NAME "\" }, " \
			 "\"enable\": {\"description\": \"A switch that can be used to enable or disable execution of " \
					 "the Python 3.5 filter.\", " \
				"\"type\": \"boolean\", " \
				"\"displayName\": \"Enabled\", " \
				"\"default\": \"false\" }, " \
			"\"config\" : {\"description\" : \"Python 3.5 filter configuration.\", " \
				"\"type\" : \"JSON\", " \
				"\"order\": \"1\", " \
				"\"displayName\" : \"Configuration\", " \
				"\"default\" : \"{}\"}, " \
			"\"script\" : {\"description\" : \"Python 3.5 module to load.\", " \
				"\"type\": \"script\", " \
				"\"order\": \"2\", " \
				"\"displayName\" : \"Python script\", " \
				"\"default\": \"""\"} }"
using namespace std;

/**
 * The Filter plugin interface
 */
extern "C" {
/**
 * The plugin information structure
 */
static PLUGIN_INFORMATION info = {
        FILTER_NAME,              // Name
        VERSION,                  // Version
        0,                        // Flags
        PLUGIN_TYPE_FILTER,       // Type
        "1.0.0",                  // Interface version
	DEFAULT_CONFIG	          // Default plugin configuration
};

typedef struct
{
	Python35Filter	*handle;
	std::string	configCatName;
} FILTER_INFO;

/**
 * Return the information about this plugin
 */
PLUGIN_INFORMATION *plugin_info()
{
	return &info;
}

/**
 * Initialise the plugin, called to get the plugin handle and setup the
 * output handle that will be passed to the output stream. The output stream
 * is merely a function pointer that is called with the output handle and
 * the new set of readings generated by the plugin.
 *     (*output)(outHandle, readings);
 * Note that the plugin may not call the output stream if the result of
 * the filtering is that no readings are to be sent onwards in the chain.
 * This allows the plugin to discard data or to buffer it for aggregation
 * with data that follows in subsequent calls
 *
 * @param config	The configuration category for the filter
 * @param outHandle	A handle that will be passed to the output stream
 * @param output	The output stream (function pointer) to which data is passed
 * @return		An opaque handle that is used in all subsequent calls to the plugin
 */
PLUGIN_HANDLE plugin_init(ConfigCategory* config,
			  OUTPUT_HANDLE *outHandle,
			  OUTPUT_STREAM output)
{
	FILTER_INFO *info = new FILTER_INFO;
	info->handle = new Python35Filter(FILTER_NAME,
						*config,
						outHandle,
						output);
	info->configCatName = config->getName();
	Python35Filter *pyFilter = info->handle;

	// Embedded Python 3.5 program name
	wchar_t *programName = Py_DecodeLocale(config->getName().c_str(), NULL);
        Py_SetProgramName(programName);
	PyMem_RawFree(programName);
	// Embedded Python 3.5 initialisation
	// Check first the interpreter is already set
	if (!Py_IsInitialized())
	{
		Py_Initialize();
		PyEval_InitThreads(); // Initialize and acquire the global interpreter lock (GIL)
		PyThreadState* save = PyEval_SaveThread(); // release GIL
		pythonInitialised = true;
	}
	
	PyGILState_STATE state = PyGILState_Ensure(); // acquire GIL
	
	// Pass FogLAMP Data dir
	pyFilter->setFiltersPath(getDataDir());

	// Set Python path for embedded Python 3.5
	// Get current sys.path. borrowed reference
	PyObject* sysPath = PySys_GetObject((char *)string("path").c_str());
	// Add FogLAMP python filters path
	PyObject* pPath = PyUnicode_DecodeFSDefault((char *)pyFilter->getFiltersPath().c_str());
	PyList_Insert(sysPath, 0, pPath);
	// Remove temp object
	Py_CLEAR(pPath);

	// Check first we have a Python script to load
	if (!pyFilter->setScriptName())
	{
		// Force disable
		pyFilter->disableFilter();

		PyGILState_Release(state);

		// Return filter handle
		return (PLUGIN_HANDLE)info;
	}
		
	// Configure filter
	if (!pyFilter->configure())
	{
		// Cleanup Python 3.5
		if (pythonInitialised)
		{
			pythonInitialised = false;
			Py_Finalize();
		}
		PyGILState_Release(state);

		// This will abort the filter pipeline set up
		return NULL;
	}
	else
	{
		PyGILState_Release(state);
		// Return filter handle
		return (PLUGIN_HANDLE)info;
	}
	PyGILState_Release(state); // release GIL
}

/**
 * Ingest a set of readings into the plugin for processing
 *
 * NOTE: in case of any error, the input readings will be passed
 * onwards (untouched)
 *
 * @param handle	The plugin handle returned from plugin_init
 * @param readingSet	The readings to process
 */
void plugin_ingest(PLUGIN_HANDLE *handle,
		   READINGSET *readingSet)
{
	FILTER_INFO *info = (FILTER_INFO *) handle;
	Python35Filter *filter = info->handle;

	// Protect against reconfiguration
	filter->lock();
	bool enabled = filter->isEnabled();
	filter->unlock();

	if (!enabled)
	{
		// Current filter is not active: just pass the readings set
		filter->m_func(filter->m_data, readingSet);
		return;
	}

        // Get all the readings in the readingset
	const vector<Reading *>& readings = ((ReadingSet *)readingSet)->getAllReadings();
	for (vector<Reading *>::const_iterator elem = readings.begin();
						      elem != readings.end();
						      ++elem)
	{
		AssetTracker::getAssetTracker()->addAssetTrackingTuple(info->configCatName, (*elem)->getAssetName(), string("Filter"));
	}
	
	/**
	 * 1 - create a Python object (list of dicts) from input data
	 * 2 - pass Python object to Python filter method
	 * 3 - Transform results from fealter into new ReadingSet
	 * 4 - Remove old data and pass new data set onwards
	 */

	PyGILState_STATE state = PyGILState_Ensure();

	// - 1 - Create Python list of dicts as input to the filter
	PyObject* readingsList = filter->createReadingsList(readings);

	// Check for errors
	if (!readingsList)
	{
		// Errors while creating Python 3.5 filter input object
		Logger::getLogger()->error("Filter '%s' (%s), script '%s', "
					   "create filter data error, action: %s",
					   FILTER_NAME,
					   filter->getConfig().getName().c_str(),
					   filter->m_pythonScript.c_str(),
					  "pass unfiltered data onwards");

		// Pass data set to next filter and return
		filter->m_func(filter->m_data, readingSet);
		PyGILState_Release(state);
		return;
	}

	// - 2 - Call Python method passing an object
	PyObject* pReturn = PyObject_CallFunction(filter->m_pFunc,
						  (char *)string("O").c_str(),
						  readingsList);

	// Free filter input data
	Py_CLEAR(readingsList);

	ReadingSet* finalData = NULL;

	// - 3 - Handle filter returned data
	if (!pReturn)
	{
		// Errors while getting result object
		Logger::getLogger()->error("Filter '%s' (%s), script '%s', "
					   "filter error, action: %s",
					   FILTER_NAME,
					   filter->getConfig().getName().c_str(),
					   filter->m_pythonScript.c_str(),
					   "pass unfiltered data onwards");

		// Errors while getting result object
		filter->logErrorMessage();

		// Filter did nothing: just pass input data
		finalData = (ReadingSet *)readingSet;
	}
	else
	{
		// Get new set of readings from Python filter
		vector<Reading *>* newReadings = filter->getFilteredReadings(pReturn);
		if (newReadings)
		{
			// Filter success
			// - Delete input data as we have a new set
			delete (ReadingSet *)readingSet;
			readingSet = NULL;

			// - Set new readings with filtered/modified data
			finalData = new ReadingSet(newReadings);

			const vector<Reading *>& readings2 = finalData->getAllReadings();
			for (vector<Reading *>::const_iterator elem = readings2.begin();
								      elem != readings2.end();
								      ++elem)
			{
				AssetTracker::getAssetTracker()->addAssetTrackingTuple(info->configCatName, (*elem)->getAssetName(), string("Filter"));
			}

			// - Remove newReadings pointer
			delete newReadings;
		}
		else
		{
			// Filtered data error: use current reading set
			finalData = (ReadingSet *)readingSet;
		}

		// Remove pReturn object
		Py_CLEAR(pReturn);
	}

	PyGILState_Release(state);

	// - 4 - Pass (new or old) data set to next filter
	filter->m_func(filter->m_data, finalData);
}

/**
 * Call the shutdown method in the plugin
 *
 * @param handle	The plugin handle returned from plugin_init
 */
void plugin_shutdown(PLUGIN_HANDLE *handle)
{
	FILTER_INFO *info = (FILTER_INFO *) handle;
	Python35Filter* filter = info->handle;

	PyGILState_STATE state = PyGILState_Ensure();

	// Decrement pFunc reference count
	Py_CLEAR(filter->m_pFunc);
		
	// Decrement pModule reference count
	Py_CLEAR(filter->m_pModule);

	//PyGILState_Release(state);
	
	// Cleanup Python 3.5
	if (pythonInitialised)
	{
		pythonInitialised = false;
		Py_Finalize();
	}

	// Remove filter object
	delete filter;

	delete info;
}

/**
 * Apply filter plugin reconfiguration
 *
 * @param    handle	The plugin handle returned from plugin_init
 * @param    newConfig	The new configuration to apply.
 */
void plugin_reconfigure(PLUGIN_HANDLE *handle, const string& newConfig)
{
	FILTER_INFO *info = (FILTER_INFO *) handle;
	Python35Filter* filter = info->handle;

	PyGILState_STATE state = PyGILState_Ensure();
	filter->reconfigure(newConfig);
	PyGILState_Release(state);
}

// End of extern "C"
};
