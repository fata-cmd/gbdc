/*************************************************************************************************
GBDHash -- Copyright (c) 2020, Markus Iser, KIT - Karlsruhe Institute of Technology

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 **************************************************************************************************/

#include <cstdio>
#include <cstdint>
#include <string>

#include "src/identify/GBDHash.h"
#include "src/identify/ISOHash.h"

#include "src/util/ResourceLimits.h"
#include "src/util/threadpool/ThreadPool.h"

#include "src/extract/CNFBaseFeatures.h"
#include "src/extract/CNFGateFeatures.h"
#include "src/extract/WCNFBaseFeatures.h"
#include "src/extract/OPBBaseFeatures.h"

#include "src/transform/IndependentSet.h"
#include "src/transform/Normalize.h"

#include "src/util/pybind11/include/pybind11/pybind11.h"
#include "src/util/pybind11/include/pybind11/stl.h"

namespace py = pybind11;

// static std::uint16_t version(PyObject *self)
// {
//     return 1;
// }

// static PyObject *cnf2kis(PyObject *self, PyObject *arg)
// {
//     const char *filename;
//     const char *output;
//     unsigned maxEdges, maxNodes;
//     unsigned rlim = 0, mlim = 0, flim = 0;
//     PyArg_ParseTuple(arg, "ssII|III", &filename, &output, &maxEdges, &maxNodes, &rlim, &mlim, &flim);

//     PyObject *dict = pydict();
//     pydict(dict, "nodes", 0);
//     pydict(dict, "edges", 0);
//     pydict(dict, "k", 0);

//     ResourceLimits limits(rlim, mlim, flim);
//     limits.set_rlimits();
//     try
//     {
//         IndependentSetFromCNF gen(filename);
//         unsigned nNodes = gen.numNodes();
//         unsigned nEdges = gen.numEdges();
//         unsigned minK = gen.minK();

//         pydict(dict, "nodes", nNodes);
//         pydict(dict, "edges", nEdges);
//         pydict(dict, "k", minK);

//         if ((maxEdges > 0 && nEdges > maxEdges) || (maxNodes > 0 && nNodes > maxNodes))
//         {
//             pydict(dict, "hash", "fileout");
//             return dict;
//         }

//         gen.generate_independent_set_problem(output);
//         pydict(dict, "local", output);

//         std::string hash = CNF::gbdhash(output);
//         pydict(dict, "hash", hash.c_str());

//         return dict;
//     }
//     catch (TimeLimitExceeded &e)
//     {
//         std::remove(output);
//         pydict(dict, "hash", "timeout");
//         return dict;
//     }
//     catch (MemoryLimitExceeded &e)
//     {
//         std::remove(output);
//         pydict(dict, "hash", "memout");
//         return dict;
//     }
//     catch (FileSizeLimitExceeded &e)
//     {
//         std::remove(output);
//         pydict(dict, "hash", "fileout");
//         return dict;
//     }
// }

// static PyObject *print_sanitized(PyObject *self, PyObject *arg)
// {
//     const char *filename;
//     unsigned rlim = 0, mlim = 0;
//     PyArg_ParseTuple(arg, "s|II", &filename, &rlim, &mlim);

//     ResourceLimits limits(rlim, mlim);
//     limits.set_rlimits();
//     try
//     {
//         sanitize(filename);
//         Py_RETURN_TRUE;
//     }
//     catch (TimeLimitExceeded &e)
//     {
//         Py_RETURN_FALSE;
//     }
//     catch (MemoryLimitExceeded &e)
//     {
//         Py_RETURN_FALSE;
//     }
// }

template <typename Extractor>
static auto feature_names()
{
    Extractor stats("");
    return stats.getNames();
}

template <typename Extractor>
void bind_threadpool(py::module &m, const std::string &type_name)
{
    py::class_<TP::ThreadPool<Extractor>>(m, type_name.c_str())
        .def(py::init<std::vector<std::string>, std::uint64_t, std::uint32_t>(),
             py::arg("paths"), py::arg("mem_max"), py::arg("jobs_max"))
        .def("get_result_queue", &TP::ThreadPool<Extractor>::get_result_queue, "Returns ownership of the queue which is used to queue results from feature extraction.")
        .def("start_threadpool", &TP::ThreadPool<Extractor>::start_threadpool, "Starts the threadpool. Has to be called after get_result_queue, otherwise extracted features can only be accessed after completion of all feature extractions.")
        .def("jobs_completed", &TP::ThreadPool<Extractor>::jobs_completed, "Returns true if all jobs have been completed, false otherwise.");
}

PYBIND11_MODULE(gbdlib, m)
{
    py::class_<MPSCQueue<TP::result_t>>(m, "MPSCQueue")
        .def(py::init<>())
        .def("pop", &MPSCQueue<TP::result_t>::pop, "Pop an element from the queue in a synchronized manner.")
        .def("empty", &MPSCQueue<TP::result_t>::empty, "Returns true if queue is empty, false otherwise.");
    bind_threadpool<CNF::BaseFeatures>(m, "ExtractBaseFeatures");
    bind_threadpool<CNFGateFeatures>(m, "ExtractGateFeatures");
    bind_threadpool<OPB::BaseFeatures>(m, "ExtractOPBBaseFeatures");
    bind_threadpool<WCNF::BaseFeatures>(m, "ExtractWCNFBaseFeatures");
    m.def("base_feature_names", &feature_names<CNF::BaseFeatures>, "Get Base Feature Names");
    m.def("gate_feature_names", &feature_names<CNFGateFeatures>, "Get Gate Feature Names");
    m.def("wcnf_base_feature_names", &feature_names<WCNF::BaseFeatures>, "Get WCNF Base Feature Names");
    m.def("opb_base_feature_names", &feature_names<OPB::BaseFeatures>, "Get OPB Base Feature Names");
    m.def("gbdhash", &CNF::gbdhash, "Calculates GBD-Hash (md5 of normalized file) of given DIMACS CNF file.");
    m.def("isohash", &WCNF::isohash, "Calculates ISO-Hash (md5 of sorted degree sequence) of given DIMACS CNF file.", py::arg("filename"));
    m.def("opbhash", &OPB::gbdhash, "Calculates OPB-Hash (md5 of normalized file) of given OPB file.");
    m.def("pqbfhash", &PQBF::gbdhash, "Calculates PQBF-Hash (md5 of normalized file) of given PQBF file.");
    m.def("wcnfhash", &WCNF::gbdhash, "Calculates WCNF-Hash (md5 of normalized file) of given WCNF file.");
    m.def("wcnfisohash", &WCNF::isohash, "Calculates WCNF ISO-Hash of given WCNF file.");
}

// static PyMethodDef myMethods[] = {
//     {"sanitize", print_sanitized, METH_VARARGS, "Print sanitized, i.e., no duplicate literals in clauses and no tautologic clauses, CNF to stdout."},
//     {"cnf2kis", cnf2kis, METH_VARARGS, "Create k-ISP Instance from given CNF Instance."},
// }
