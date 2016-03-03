// Attempts to load a UCL structure from a string
#include <ucl.h>
#include <Python.h>

static PyObject *
_basic_ucl_type (ucl_object_t const *obj)
{
	switch (obj->type) {
	case UCL_INT:
		return Py_BuildValue ("L", (long long)ucl_object_toint (obj));
	case UCL_FLOAT:
		return Py_BuildValue ("d", ucl_object_todouble (obj));
	case UCL_STRING:
		return Py_BuildValue ("s", ucl_object_tostring (obj));
	case UCL_BOOLEAN:
		return ucl_object_toboolean (obj) ? Py_True : Py_False;
	case UCL_TIME:
		return Py_BuildValue ("d", ucl_object_todouble (obj));
	}
	return NULL;
}

static PyObject *
_iterate_valid_ucl (ucl_object_t const *obj)
{
	const ucl_object_t *tmp;
	ucl_object_iter_t it = NULL;

	tmp = obj;

	while ((obj = ucl_object_iterate (tmp, &it, false))) {
		PyObject *val;

		val = _basic_ucl_type(obj);
		if (!val) {
			PyObject *key = NULL;

			if (obj->key != NULL) {
				key = Py_BuildValue("s", ucl_object_key(obj));
			}

			if (obj->type == UCL_OBJECT) {
				const ucl_object_t *cur;
				ucl_object_iter_t it_obj = NULL;

				val = PyDict_New();

				while ((cur = ucl_object_iterate (obj, &it_obj, true))) {
					PyObject *keyobj = Py_BuildValue("s",ucl_object_key(cur));
					PyDict_SetItem(val, keyobj, _iterate_valid_ucl(cur));
				}
			} else if (obj->type == UCL_ARRAY) {
				const ucl_object_t *cur;
				ucl_object_iter_t it_obj = NULL;

				val = PyList_New(0);

				while ((cur = ucl_object_iterate (obj, &it_obj, true))) {
					PyList_Append(val, _iterate_valid_ucl(cur));
				}
			} else if (obj->type == UCL_USERDATA) {
				// XXX: this should be
				// PyBytes_FromStringAndSize; where is the
				// length from?
				val = PyBytes_FromString(obj->value.ud);
			}
		}
		return val;
	}

	PyErr_SetString(PyExc_SystemError, "unhandled type");
	return NULL;
}

static PyObject *
_internal_load_ucl (char *uclstr)
{
	PyObject *ret;
	struct ucl_parser *parser = ucl_parser_new (UCL_PARSER_NO_TIME);
	bool r = ucl_parser_add_string(parser, uclstr, 0);

	if (r) {
		if (ucl_parser_get_error (parser)) {
			PyErr_SetString(PyExc_ValueError, ucl_parser_get_error(parser));
			ucl_parser_free(parser);
			ret = NULL;
			goto return_with_parser;
		} else {
			ucl_object_t *uclobj = ucl_parser_get_object(parser);
			ret = _iterate_valid_ucl(uclobj);
			ucl_object_unref(uclobj);
			goto return_with_parser;
		}
	}
	else {
		PyErr_SetString(PyExc_ValueError, ucl_parser_get_error (parser));
		ret = NULL;
		goto return_with_parser;
	}

return_with_parser:
	ucl_parser_free(parser);
	return ret;
}

static PyObject*
ucl_load (PyObject *self, PyObject *args)
{
	char *uclstr;

	if (PyArg_ParseTuple(args, "z", &uclstr)) {
		if (!uclstr) {
			Py_RETURN_NONE;
		}

		return _internal_load_ucl(uclstr);
	}

	return NULL;
}

static ucl_object_t *
_iterate_python (PyObject *obj)
{
	if (obj == Py_None) {
		return ucl_object_new();
	} else if (PyBool_Check (obj)) {
		return ucl_object_frombool (obj == Py_True);
	} else if (PyInt_Check (obj)) {
		return ucl_object_fromint (PyInt_AsLong (obj));
	} else if (PyFloat_Check (obj)) {
		return ucl_object_fromdouble (PyFloat_AsDouble (obj));
	} else if (PyString_Check (obj)) {
		return ucl_object_fromstring (PyString_AsString (obj));
	// } else if (PyDateTime_Check (obj)) {
	}
	else if (PyDict_Check(obj)) {
		PyObject *key, *value;
		Py_ssize_t pos = 0;
		ucl_object_t *top, *elm;

		top = ucl_object_typed_new (UCL_OBJECT);

		while (PyDict_Next(obj, &pos, &key, &value)) {
			elm = _iterate_python(value);
			ucl_object_insert_key (top, elm, PyString_AsString(key), 0, true);
		}

		return top;
	}
	else if (PySequence_Check(obj)) {
		PyObject *value;
		Py_ssize_t len, pos;
		ucl_object_t *top, *elm;

		len  = PySequence_Length(obj);
		top = ucl_object_typed_new (UCL_ARRAY);

		for (pos = 0; pos < len; pos++) {
			value = PySequence_GetItem(obj, pos);
			elm = _iterate_python(value);
			ucl_array_append(top, elm);
		}

		return top;
	}
	else {
		PyErr_SetString(PyExc_TypeError, "Unhandled object type");
		return NULL;
	}

	return NULL;
}

static PyObject *
ucl_dump (PyObject *self, PyObject *args)
{
	PyObject *obj;
	ucl_object_t *root = NULL;

	if (!PyArg_ParseTuple(args, "O", &obj)) {
		PyErr_SetString(PyExc_TypeError, "Unhandled object type");
		return NULL;
	}

	if (obj == Py_None) {
		Py_RETURN_NONE;
	}

	if (!PyDict_Check(obj)) {
		PyErr_SetString(PyExc_TypeError, "Argument must be dict");
		return NULL;
	}

	root = _iterate_python(obj);
	if (root) {
		PyObject *ret;
		char *buf = (char *) ucl_object_emit(root, UCL_EMIT_CONFIG);
		ucl_object_unref(root);
		ret = PyString_FromString(buf);
		free(buf);
		return ret;
	}

	return NULL;
}

static PyObject *
ucl_validate (PyObject *self, PyObject *args)
{
	char *uclstr, *schema;

	if (PyArg_ParseTuple(args, "zz", &uclstr, &schema)) {
		if (!uclstr || !schema) {
			Py_RETURN_NONE;
		}

		PyErr_SetString(PyExc_NotImplementedError, "schema validation is not yet supported");
	}

	return NULL;
}

static PyMethodDef uclMethods[] = {
	{"load", ucl_load, METH_VARARGS, "Load UCL from stream"},
	{"dump", ucl_dump, METH_VARARGS, "Dump UCL to stream"},
	{"validate", ucl_validate, METH_VARARGS, "Validate ucl stream against schema"},
	{NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef uclmodule = {
	PyModuleDef_HEAD_INIT,
	"ucl",
	NULL,
	-1,
	uclMethods
};

PyMODINIT_FUNC
PyInit_ucl (void)
{
	return PyModule_Create (&uclmodule);
}
#else
void initucl (void)
{
	Py_InitModule ("ucl", uclMethods);
}
#endif
