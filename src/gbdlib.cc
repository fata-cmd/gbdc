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
#include <chrono>
#include <regex>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "src/identify/GBDHash.h"
#include "src/identify/ISOHash.h"

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

static std::string version()
{
    std::ifstream file("setup.py");
    if (!file.is_open())
    {
        return "Error: Could not open setup.py";
    }

    std::string line;
    std::regex version_regex(R"(version\s*=\s*['"]([^'"]+)['"])");
    std::smatch match;

    while (std::getline(file, line))
    {
        if (std::regex_search(line, match, version_regex))
        {
            return match[1].str();
        }
    }

    return "Error: Version not found in setup.py";
}

static auto cnf2kis(std::string filename, std::string output, size_t maxEdges, size_t maxNodes)
{
    std::unordered_map<std::string, std::string> dict;

    IndependentSetFromCNF gen(filename.c_str());
    unsigned nNodes = gen.numNodes();
    unsigned nEdges = gen.numEdges();
    unsigned minK = gen.minK();

    dict["nodes"] = std::to_string(nNodes);
    dict["edges"] = std::to_string(nEdges);
    dict["k"] = std::to_string(minK);

    if ((maxEdges > 0 && nEdges > maxEdges) || (maxNodes > 0 && nNodes > maxNodes))
    {
        dict["hash"] = "fileout";
        return dict;
    }

    gen.generate_independent_set_problem(output.c_str());
    dict["local"] = output;

    std::string hash = CNF::gbdhash(output.c_str());
    dict["hash"] = hash.c_str();
    return dict;
}

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
             "Thread pool used for parallel feature extraction. Takes a list of paths to CNF instances, the maximum amount of memory available and the maximum amount of parallel jobs permitted.",
             py::arg("paths"), py::arg("mem_max"), py::arg("jobs_max"))
        .def("get_result_queue", &TP::ThreadPool<Extractor>::get_result_queue, "Returns ownership of the queue which is used to output results from feature extraction.")
        .def("start_threadpool", &TP::ThreadPool<Extractor>::start_threadpool, "Starts the threadpool. Has to be called after 'get_result_queue', otherwise extracted features can only be accessed after completion of all feature extractions.")
        .def("jobs_completed", &TP::ThreadPool<Extractor>::jobs_completed, "Returns true if all jobs have been completed, false otherwise.");
}

PYBIND11_MODULE(gbdlib, m)
{
    py::class_<MPSCQueue<TP::result_t>>(m, "MPSCQueue")
        .def(py::init<>())
        .def("pop", &MPSCQueue<TP::result_t>::pop, "Pop an element from the queue in a synchronized manner.")
        .def("empty", &MPSCQueue<TP::result_t>::empty, "Returns true if queue is empty, false otherwise.");
    bind_threadpool<CNF::BaseFeatures>(m, "extract_base_features");
    bind_threadpool<CNFGateFeatures>(m, "extract_gate_features");
    bind_threadpool<OPB::BaseFeatures>(m, "extract_opb_base_features");
    bind_threadpool<WCNF::BaseFeatures>(m, "extract_wcnf_base_features");
    m.def("version", &version, "Return current version of gbdc.");
    m.def("cnf2kis", &cnf2kis, "Create k-ISP Instance from given CNF Instance.", py::arg("filename"), py::arg("output"), py::arg("maxEdges"), py::arg("maxNodes"));
    m.def("sanitize", &sanitize, "Print sanitized, i.e., no duplicate literals in clauses and no tautologic clauses, CNF to stdout.", py::arg("filename"));
    m.def("base_feature_names", &feature_names<CNF::BaseFeatures>, "Get Base Feature Names");
    m.def("gate_feature_names", &feature_names<CNFGateFeatures>, "Get Gate Feature Names");
    m.def("wcnf_base_feature_names", &feature_names<WCNF::BaseFeatures>, "Get WCNF Base Feature Names");
    m.def("opb_base_feature_names", &feature_names<OPB::BaseFeatures>, "Get OPB Base Feature Names");
    m.def("gbdhash", &CNF::gbdhash, "Calculates GBD-Hash (md5 of normalized file) of given DIMACS CNF file.", py::arg("filename"));
    m.def("isohash", &WCNF::isohash, "Calculates ISO-Hash (md5 of sorted degree sequence) of given DIMACS CNF file.", py::arg("filename"));
    m.def("opbhash", &OPB::gbdhash, "Calculates OPB-Hash (md5 of normalized file) of given OPB file.", py::arg("filename"));
    m.def("pqbfhash", &PQBF::gbdhash, "Calculates PQBF-Hash (md5 of normalized file) of given PQBF file.", py::arg("filename"));
    m.def("wcnfhash", &WCNF::gbdhash, "Calculates WCNF-Hash (md5 of normalized file) of given WCNF file.", py::arg("filename"));
    m.def("wcnfisohash", &WCNF::isohash, "Calculates WCNF ISO-Hash of given WCNF file.", py::arg("filename"));
}
