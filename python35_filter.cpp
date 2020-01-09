/*
 * FogLAMP "Python 3.5" filter plugin.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <iostream>

#define PYTHON_SCRIPT_METHOD_PREFIX "_script_"
#define PYTHON_SCRIPT_FILENAME_EXTENSION ".py"
#define SCRIPT_CONFIG_ITEM_NAME "script"
// Filter configuration method
#define DEFAULT_FILTER_CONFIG_METHOD "set_filter_config"

#include "python35.h"

using namespace std;

/**
 * Create a Python 3.5 object (list of dicts)
 * to be passed to Python 3.5 loaded filter
 *
 * @param readings	The input readings
 * @return		PyObject pointer (list of dicts)
 *			or NULL in case of errors
 */
PyObject* Python35Filter::createReadingsList(const vector<Reading *>& readings)
{
	// TODO add checks to all PyList_XYZ methods
	PyObject* readingsList = PyList_New(0);

	// Iterate the input readings
	for (vector<Reading *>::const_iterator elem = readings.begin();
                                                      elem != readings.end();
                                                      ++elem)
	{
		// Create an object (dict) with 'asset_code' and 'readings' key
		PyObject* readingObject = PyDict_New();

		// Create object (dict) for reading Datapoints:
		// this will be added as vale for key 'readings'
		PyObject* newDataPoints = PyDict_New();

		// Get all datapoints
		std::vector<Datapoint *>& dataPoints = (*elem)->getReadingData();
		for (auto it = dataPoints.begin(); it != dataPoints.end(); ++it)
		{
			PyObject* value;
			DatapointValue::dataTagType dataType = (*it)->getData().getType();

			if (dataType == DatapointValue::dataTagType::T_INTEGER)
			{
				value = PyLong_FromLong((*it)->getData().toInt());
			}
			else if (dataType == DatapointValue::dataTagType::T_FLOAT)
			{
				value = PyFloat_FromDouble((*it)->getData().toDouble());
			}
			else
			{
				value = PyBytes_FromString((*it)->getData().toString().c_str());
			}

			// Add Datapoint: key and value
			PyObject* key = PyBytes_FromString((*it)->getName().c_str());
			PyDict_SetItem(newDataPoints,
					key,
					value);
			
			Py_CLEAR(key);
			Py_CLEAR(value);
		}

		// Add reading datapoints
		PyDict_SetItemString(readingObject, "reading", newDataPoints);

		// Add reading asset name
		PyObject* assetVal = PyBytes_FromString((*elem)->getAssetName().c_str());
		PyDict_SetItemString(readingObject, "asset_code", assetVal);

		/**
		 * Save id, timestamp and user_timestamp
		 */

		// Add reading id
		PyObject* readingId = PyLong_FromUnsignedLong((*elem)->getId());
		PyDict_SetItemString(readingObject, "id", readingId);

		// Add reading timestamp
		PyObject* readingTs = PyLong_FromUnsignedLong((*elem)->getTimestamp());
		PyDict_SetItemString(readingObject, "ts", readingTs);

		// Add reading user timestamp
		PyObject* readingUserTs = PyLong_FromUnsignedLong((*elem)->getUserTimestamp());
		PyDict_SetItemString(readingObject, "user_ts", readingUserTs);

		// Add new object to the list
		PyList_Append(readingsList, readingObject);

		// Remove temp objects
		Py_CLEAR(newDataPoints);
		Py_CLEAR(assetVal);
		Py_CLEAR(readingId);
		Py_CLEAR(readingTs);
		Py_CLEAR(readingUserTs);
		Py_CLEAR(readingObject);
	}

	// Return pointer of new allocated list
	return readingsList;
}

/**
 * Get the vector of filtered readings from Python 3.5 script
 *
 * @param filteredData	Python 3.5 Object (list of dicts)
 * @return		Pointer to a new allocated vector<Reading *>
 *			or NULL in case of errors
 * Note:
 * new readings have:
 * - new timestamps
 * - new UUID
 */
vector<Reading *>* Python35Filter::getFilteredReadings(PyObject* filteredData)
{
	// Create result set
	vector<Reading *>* newReadings = new vector<Reading *>();

	// Iterate filtered data in the list
	for (int i = 0; i < PyList_Size(filteredData); i++)
	{
		// Get list item: borrowed reference.
		PyObject* element = PyList_GetItem(filteredData, i);
		if (!element)
		{
			// Failure
			if (PyErr_Occurred())
			{
				this->logErrorMessage();
			}
			delete newReadings;

			return NULL;
		}

		// Get 'asset_code' value: borrowed reference.
		PyObject* assetCode = PyDict_GetItemString(element,
							   "asset_code");
		// Get 'reading' value: borrowed reference.
		PyObject* reading = PyDict_GetItemString(element,
							 "reading");
		// Keys not found or reading is not a dict
		if (!assetCode ||
		    !reading ||
		    !PyDict_Check(reading))
		{
			// Failure
			if (PyErr_Occurred())
			{
				this->logErrorMessage();
			}
			delete newReadings;

			return NULL;
		}

		// Fetch all Datapoins in 'reading' dict			
		PyObject *dKey, *dValue;
		Py_ssize_t dPos = 0;
		Reading* newReading = NULL;

		// Fetch all Datapoins in 'reading' dict
		// dKey and dValue are borrowed references
		while (PyDict_Next(reading, &dPos, &dKey, &dValue))
		{
			DatapointValue* dataPoint;
			if (PyLong_Check(dValue) || PyLong_Check(dValue))
			{
				dataPoint = new DatapointValue((long)PyLong_AsUnsignedLongMask(dValue));
			}
			else if (PyFloat_Check(dValue))
			{
				dataPoint = new DatapointValue(PyFloat_AS_DOUBLE(dValue));
			}
			else if (PyBytes_Check(dValue))
			{
				dataPoint = new DatapointValue(string(PyBytes_AsString(dValue)));
			}
			else
			{
				delete newReadings;
				delete dataPoint;

				return NULL;
			}

			// Add / Update the new Reading data			
			if (newReading == NULL)
			{
				newReading = new Reading(string(PyBytes_AsString(assetCode)),
							 new Datapoint(string(PyBytes_AsString(dKey)),
								       *dataPoint));
			}
			else
			{
				newReading->addDatapoint(new Datapoint(string(PyBytes_AsString(dKey)),
								       *dataPoint));
			}

			/**
			 * Set id, uuid, ts and user_ts of the original data
			 */

			// Get 'id' value: borrowed reference.
			PyObject* id = PyDict_GetItemString(element, "id");
			if (id && PyLong_Check(id))
			{
				// Set id
				newReading->setId(PyLong_AsUnsignedLong(id));
			}

			// Get 'ts' value: borrowed reference.
			PyObject* ts = PyDict_GetItemString(element, "ts");
			if (ts && PyLong_Check(ts))
			{
				// Set timestamp
				newReading->setTimestamp(PyLong_AsUnsignedLong(ts));
			}

			// Get 'user_ts' value: borrowed reference.
			PyObject* uts = PyDict_GetItemString(element, "user_ts");
			if (uts && PyLong_Check(uts))
			{
				// Set user timestamp
				newReading->setUserTimestamp(PyLong_AsUnsignedLong(uts));
			}

			// Remove temp objects
			delete dataPoint;
		}

		if (newReading)
		{
			// Add the new reading to result vector
			newReadings->push_back(newReading);
		}
	}

	return newReadings;
}

/**
 * Log current Python 3.5 error message
 */
void Python35Filter::logErrorMessage()
{
#ifdef PYTHON_CONSOLE_DEBUG
	// Print full Python stacktrace 
	PyErr_Print();
#endif
	//Get error message
	PyObject *pType, *pValue, *pTraceback;
	PyErr_Fetch(&pType, &pValue, &pTraceback);
	PyErr_NormalizeException(&pType, &pValue, &pTraceback);

	PyObject* str_exc_value = PyObject_Repr(pValue);
	PyObject* pyExcValueStr = PyUnicode_AsEncodedString(str_exc_value, "utf-8", "Error ~");

	// NOTE from :
	// https://docs.python.org/2/c-api/exceptions.html
	//
	// The value and traceback object may be NULL
	// even when the type object is not.	
	const char* pErrorMessage = pValue ?
				    PyBytes_AsString(pyExcValueStr) :
				    "no error description.";

	Logger::getLogger()->fatal("Filter '%s', script "
				   "'%s': Error '%s'",
				   this->getName().c_str(),
				   m_pythonScript.c_str(),
				   pErrorMessage);

	// Reset error
	PyErr_Clear();

	// Remove references
	Py_CLEAR(pType);
	Py_CLEAR(pValue);
	Py_CLEAR(pTraceback);
	Py_CLEAR(str_exc_value);
	Py_CLEAR(pyExcValueStr);
}

/**
 * Reconfigure Python35 filter with new configuration
 *
 * @param    newConfig		The new configuration
 *				from "plugin_reconfigure"
 * @return			True on success, false on errors.
 */
bool Python35Filter::reconfigure(const string& newConfig)
{
	Logger::getLogger()->debug("%s filter 'plugin_reconfigure' called = %s",
				   this->getName().c_str(),
				   newConfig.c_str());

	ConfigCategory category("new", newConfig);
	string newScript;

	// Configuration change is protected by a lock
	lock_guard<mutex> guard(m_configMutex);

	PyGILState_STATE state = PyGILState_Ensure(); // acquire GIL

	// Get Python script file from "file" attibute of "scipt" item
	if (category.itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "scipt" item
			newScript = category.getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
								   ConfigCategory::FILE_ATTR);

			// Just take file name and remove path
			std::size_t found = newScript.find_last_of("/");
			if (found != std::string::npos)
			{
				newScript = newScript.substr(found + 1);

				// Remove .py from pythonScript
				found = newScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
				if (found != std::string::npos)
				{
					newScript.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
				}
			}
		}
		catch (ConfigItemAttributeNotFound* e)
		{
			delete e;
		}
		catch (exception* e)
		{
			delete e;
		}
	}

	if (newScript.empty())
	{
		Logger::getLogger()->warn("Filter '%s', "
					  "called without a Python 3.5 script. "
					  "Check 'script' item in '%s' configuration. "
					  "Filter has been disabled.",
					  this->getName().c_str(),
					  this->getName().c_str());
		// Force disable
		PyGILState_Release(state);
		this->disableFilter();
		return false;
	}

	// Reload module or Import module ?
	if (newScript.compare(m_pythonScript) == 0)
	{
		// Reimport module
		PyObject* newModule = PyImport_ReloadModule(m_pModule);
		if (newModule)
		{
			// Cleanup Loaded module
			Py_CLEAR(m_pModule);
			m_pModule = NULL;
			Py_CLEAR(m_pFunc);
			m_pFunc = NULL;

			// Set new name
			m_pythonScript = newScript;

			// Set reloaded module
			m_pModule = newModule;
		}
		else
		{
			// Errors while reloading the Python module
			Logger::getLogger()->error("%s filter error while reloading "
						   " Python script '%s' in 'plugin_reconfigure'",
						   this->getName().c_str(),
						   m_pythonScript.c_str());
			logErrorMessage();

			PyGILState_Release(state);

			return false;
		}
	}
	else
	{
		// Import the new module

		// Cleanup Loaded module
		Py_CLEAR(m_pModule);
		m_pModule = NULL;
		Py_CLEAR(m_pFunc);
		m_pFunc = NULL;

		// Set new name
		m_pythonScript = newScript;

		// Import the new module
		PyObject* newModule = PyImport_ImportModule(m_pythonScript.c_str());

		// Set reloaded module
		m_pModule = newModule;
	}

	// Set the enable flag
	if (category.itemExists("enable"))
	{
		m_enabled = category.getValue("enable").compare("true") == 0 ||
				category.getValue("enable").compare("True") == 0;
	}

	bool ret = this->configure();

	PyGILState_Release(state);

	return ret;
}


/**
 * Configure Python35 filter:
 *
 * import the Python script file and call
 * script configuration method with current filter configuration
 *
 * @return	True on success, false on errors.
 */
bool Python35Filter::configure()
{
	// Import script as module
	// NOTE:
	// Script file name is:
	// lowercase(categoryName) + _script_ + methodName + ".py"
	
	string filterMethod;
	std::size_t found;

	Logger::getLogger()->debug("%s:%d: m_pythonScript=%s", __FUNCTION__, __LINE__, m_pythonScript.c_str());

	// 1) Get methodName
	found = m_pythonScript.rfind(PYTHON_SCRIPT_METHOD_PREFIX);
	if (found != std::string::npos)
	{
		filterMethod = m_pythonScript.substr(found + strlen(PYTHON_SCRIPT_METHOD_PREFIX));
	}
	// Remove .py from filterMethod
	found = filterMethod.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	if (found != std::string::npos)
	{
		filterMethod.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
	}
	// Remove .py from pythonScript
	found = m_pythonScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	if (found != std::string::npos)
	{
		m_pythonScript.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
	}
	
	Logger::getLogger()->debug("%s filter: script='%s', method='%s'",
				   this->getName().c_str(),
				   m_pythonScript.c_str(),
				   filterMethod.c_str());

	// 2) Import Python script
	// check first method name is empty:
	// disable filter, cleanup and return true
	// This allows reconfiguration
		if (filterMethod.empty())
	{
		// Force disable
		this->disableFilter();

		m_pModule = NULL;
		m_pFunc = NULL;

		return true;
	}

	// 2) Import Python script if module object is not set
	if (!m_pModule)
	{
		m_pModule = PyImport_ImportModule(m_pythonScript.c_str());
	}

	// Check whether the Python module has been imported
	if (!m_pModule)
	{
		// Failure
		if (PyErr_Occurred())
		{
			this->logErrorMessage();
		}
		Logger::getLogger()->fatal("Filter '%s', cannot import Python 3.5 script "
					   "'%s' from '%s'",
					   this->getName().c_str(),
					   m_pythonScript.c_str(),
					   m_filtersPath.c_str());

		// This will abort the filter pipeline set up
		return false;
	}

	// Fetch filter method in loaded object
	m_pFunc = PyObject_GetAttrString(m_pModule, filterMethod.c_str());

	if (!PyCallable_Check(m_pFunc))
	{
		// Failure
		if (PyErr_Occurred())
		{
			this->logErrorMessage();
		}

		Logger::getLogger()->fatal("Filter %s error: cannot find Python 3.5 method "
					   "'%s' in loaded module '%s.py'",
					   this->getName().c_str(),
					   filterMethod.c_str(),
					   m_pythonScript.c_str());
		Py_CLEAR(m_pModule);
		m_pModule = NULL;
		Py_CLEAR(m_pFunc);
		m_pFunc = NULL;

		// This will abort the filter pipeline set up
		return false;
	}

	// Whole configuration as it is
	string filterConfiguration;

	// Get 'config' filter category configuration
	if (this->getConfig().itemExists("config"))
	{
		filterConfiguration = this->getConfig().getValue("config");
	}
	else
	{
		// Set empty object
		filterConfiguration = "{}";
	}

	/**
	 * We now pass the filter JSON configuration to the loaded module
	 */
	PyObject* pConfigFunc = PyObject_GetAttrString(m_pModule,
							   (char *)string(DEFAULT_FILTER_CONFIG_METHOD).c_str());
	// Check whether "set_filter_config" method exists
	if (PyCallable_Check(pConfigFunc))
	{
		// Set configuration object 
		PyObject* pConfig = PyDict_New();
		// Add JSON configuration, as string, to "config" key
		PyObject* pConfigObject = PyUnicode_DecodeFSDefault(filterConfiguration.c_str());
		PyDict_SetItemString(pConfig,
					 "config",
					 pConfigObject);
		Py_CLEAR(pConfigObject);
		/**
		 * Call method set_filter_config(c)
		 * This creates a global JSON configuration
		 * which will be available when fitering data with "plugin_ingest"
		 *
		 * set_filter_config(config) returns 'True'
		 */
		//PyObject* pSetConfig = PyObject_CallMethod(pModule,
		PyObject* pSetConfig = PyObject_CallFunctionObjArgs(pConfigFunc,
									// arg 1
									pConfig,
									// end of args
									NULL);

		// Check result
		if (!pSetConfig ||
			!PyBool_Check(pSetConfig) ||
			!PyLong_AsLong(pSetConfig))
		{
			this->logErrorMessage();

			Py_CLEAR(m_pModule);
			m_pModule = NULL;
			Py_CLEAR(m_pFunc);
			m_pFunc = NULL;
			// Remove temp objects
			Py_CLEAR(pConfig);
			Py_CLEAR(pSetConfig);

			// Remove function object
			Py_CLEAR(pConfigFunc);

			return false;
		}
		// Remove call object
		Py_CLEAR(pSetConfig);
		// Remove temp objects
		Py_CLEAR(pConfig);
	}
	else
	{
		// Reset error if config function is not present
		PyErr_Clear();
	}

	// Remove function object
	Py_CLEAR(pConfigFunc);

	return true;
}

/**
 * Set the Python script name to load.
 *
 * If the attribute "file" of "script" items exists
 * in input configuration the m_pythonScript member is updated.
 *
 * This method must be called before Python35Filter::configure()
 *
 * @return	True if script file exists, false otherwise
 */
bool Python35Filter::setScriptName()
{
	// Check whether we have a Python 3.5 script file to import
	if (this->getConfig().itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "script" item
			m_pythonScript =
				this->getConfig().getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
								   ConfigCategory::FILE_ATTR);
			// Just take file name and remove path
			std::size_t found = m_pythonScript.find_last_of("/");
			m_pythonScript = m_pythonScript.substr(found + 1);
		}
		catch (ConfigItemAttributeNotFound* e)
		{
			delete e;
		}
		catch (exception* e)
		{
			delete e;
		}
	}

	if (m_pythonScript.empty())
	{
		// Do nothing
		Logger::getLogger()->warn("Filter '%s', "
					  "called without a Python 3.5 script. "
					  "Check 'script' item in '%s' configuration. "
					  "Filter has been disabled.",
					  this->getName().c_str(),
					  this->getConfig().getName().c_str());
	}

	return !m_pythonScript.empty();
}
