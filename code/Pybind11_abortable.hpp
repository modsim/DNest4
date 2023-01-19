#ifndef DNEST4_PYBIND11_ABORTABLE_HPP
#define DNEST4_PYBIND11_ABORTABLE_HPP

#ifdef DNEST4_FROM_PYBIND11

#include <pybind11/detail/common.h>

#define DNEST4_ABORTABLE if (PyErr_CheckSignals() != 0) throw pybind11::error_already_set();

#else

#define DNEST4_ABORTABLE

#endif //DNEST4_FROM_PYBIND11


#endif //DNEST4_PYBIND11_ABORTABLE_HPP
