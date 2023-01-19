#ifndef HOPS_PYBIND11_ABORTABLE_HPP
#define HOPS_PYBIND11_ABORTABLE_HPP

#ifdef DNEST4_FROM_PYBIND11

#include <pybind11/detail/common.h>

#define ABORTABLE if (PyErr_CheckSignals() != 0) throw pybind11::error_already_set();

#else

#define ABORTABLE

#endif //DNEST4_FROM_PYBIND11


#endif //HOPS_PYBIND11_ABORTABLE_HPP
