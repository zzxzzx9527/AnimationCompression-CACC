// Convert decoded quantized levels to NumPy frames.
#define PY_SSIZE_T_CLEAN
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <numpy/arrayobject.h>
#include <windows.h>
#include <cstdint>

using Feed=int64_t(*)(void*,const uint8_t*,int64_t,void**);

static PyObject* feed_frames(Feed decode,PyObject*h,PyObject*chunk,int channels,int bits){
 void*handle=PyLong_AsVoidPtr(h);if(PyErr_Occurred())return nullptr;
 if(!decode||!handle||channels<1||channels>93){PyErr_SetString(PyExc_ValueError,"Closed");return nullptr;}
 const auto*data=reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(chunk));
 int64_t length=PyBytes_GET_SIZE(chunk),status;void*result=nullptr;
 Py_BEGIN_ALLOW_THREADS
 status=decode(handle,data,length,&result);
 Py_END_ALLOW_THREADS
 if(status<0){PyErr_Format(PyExc_ValueError,"Feed %lld",(long long)status);return nullptr;}
 int64_t frames=status>>1;
 npy_intp shape[2]={npy_intp(frames),npy_intp(channels)};
 auto*out=reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(2,shape,NPY_FLOAT32));
 if(!out)return nullptr;
 const size_t values=size_t(frames)*size_t(channels);
  const auto* levels=static_cast<const uint16_t*>(result);
  const float scale=1.0f/float(1u<<bits);
  auto* output=static_cast<float*>(PyArray_DATA(out));
  for(size_t index=0;index<values;++index)output[index]=levels[index]*scale;
 PyObject*ended=PyBool_FromLong(status&1);
 if(!ended){Py_DECREF(out);return nullptr;}
 PyObject*tuple=PyTuple_New(2);
 if(!tuple){Py_DECREF(out);Py_DECREF(ended);return nullptr;}
 PyTuple_SET_ITEM(tuple,0,reinterpret_cast<PyObject*>(out));PyTuple_SET_ITEM(tuple,1,ended);
 return tuple;
}

static Feed low_memory_feed_fn=nullptr;

static PyObject* bind_low_memory(PyObject*,PyObject* path){
 if(low_memory_feed_fn)Py_RETURN_NONE;
 wchar_t* library_path=PyUnicode_AsWideCharString(path,nullptr);
 if(!library_path)return nullptr;
 HMODULE library=LoadLibraryW(library_path);
 PyMem_Free(library_path);
 if(!library){PyErr_SetString(PyExc_ImportError,"Cannot load low-memory decoder");return nullptr;}
 low_memory_feed_fn=reinterpret_cast<Feed>(GetProcAddress(library,"cacc_low_memory_feed_levels"));
 if(!low_memory_feed_fn){FreeLibrary(library);PyErr_SetString(PyExc_ImportError,"Missing low-memory decoder");return nullptr;}
 Py_RETURN_NONE;
}

static PyObject* feed_low_memory(PyObject*,PyObject*args){
 PyObject*h,*chunk;int channels,bits;
 if(!PyArg_ParseTuple(args,"OSii",&h,&chunk,&channels,&bits))return nullptr;
 if(bits<2||bits>14){PyErr_SetString(PyExc_ValueError,"Invalid precision");return nullptr;}
 return feed_frames(low_memory_feed_fn,h,chunk,channels,bits);
}

static PyMethodDef methods[]={
 {"bind_low_memory",bind_low_memory,METH_O,nullptr},{"feed_low_memory",feed_low_memory,METH_VARARGS,nullptr},
 {nullptr,nullptr,0,nullptr}};
static PyModuleDef definition={PyModuleDef_HEAD_INIT,"_cacc_buffer_bridge",nullptr,-1,methods};
PyMODINIT_FUNC PyInit__cacc_buffer_bridge(){import_array();return PyModule_Create(&definition);}
