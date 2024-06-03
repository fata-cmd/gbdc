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
#include <variant>

#include "src/identify/GBDHash.h"
#include "src/identify/ISOHash.h"

#include "src/util/threadpool/ThreadPool.h"

#include "src/extract/CNFBaseFeatures.h"
#include "src/extract/CNFGateFeatures.h"
#include "src/extract/WCNFBaseFeatures.h"
#include "src/extract/OPBBaseFeatures.h"

#include "src/transform/IndependentSet.h"
#include "src/transform/Normalize.h"
#include "src/util/ResourceLimits.h"

#include "src/util/pybind11/include/pybind11/pybind11.h"
#include "src/util/pybind11/include/pybind11/stl.h"
#include "src/util/pybind11/include/pybind11/functional.h"

namespace py = pybind11;
namespace tp = threadpool;

enum class Status
{
    TIMEOUT = -1,
    MEMOUT = -2
};

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
static std::unordered_map<std::string, std::variant<double, std::string>> extract_features(const std::string filepath, const size_t rlim, const size_t mlim)
{
    std::unordered_map<std::string, std::variant<double, std::string>> emergency;
    ResourceLimits limits(rlim, mlim);
    limits.set_rlimits();
    try
    {
        Extractor stats(filepath.c_str());
        stats.extract();
        std::unordered_map<std::string, std::variant<double, std::string>> dict;
        const auto names = stats.getNames();
        const auto features = stats.getFeatures();
        dict["base_features_runtime"] = (double)limits.get_runtime();
        for (size_t i = 0; i < features.size(); ++i)
        {
            dict[names[i]] = features[i];
        }
        return dict;
    }
    catch (TimeLimitExceeded &e)
    {
        emergency["base_features_runtime"] = "timeout";
        return emergency;
    }
    catch (MemoryLimitExceeded &e)
    {
        emergency["base_features_runtime"] = "memout";
        return emergency;
    }
}

template <typename Extractor>
static std::unordered_map<std::string, double> extract_features(const std::string filepath)
{
    Extractor stats(filepath.c_str());
    stats.extract();
    std::unordered_map<std::string, double> dict;
    const auto names = stats.getNames();
    const auto features = stats.getFeatures();
    for (size_t i = 0; i < features.size(); ++i)
    {
        dict[names[i]] = features[i];
    }
    return dict;
}

template <typename Extractor>
static auto feature_names()
{
    Extractor stats("");
    return stats.getNames();
}

template <typename Ret, typename... Args>
static void bind_threadpool(py::module &m, const std::string &type_name)
{
    py::class_<tp::ThreadPool<Ret, Args...>>(m, type_name.c_str())
        .def(py::init<std::uint64_t, std::uint32_t, std::function<Ret(Args...)>, std::vector<std::string>>(),
             "Thread pool used for parallel feature extraction.",
             py::arg("mem_max"), py::arg("jobs_max"), py::arg("extractor_function"), py::arg("paths"))
        .def("get_result_queue", &tp::ThreadPool<Ret, Args...>::get_result_queue, py::return_value_policy::reference, "Returns reference to the queue which is used to output results from feature extraction.")
        .def("start_threadpool", &tp::ThreadPool<Ret, Args...>::start_threadpool, "Starts the threadpool. Has to be called after 'get_result_queue', otherwise extracted features can only be accessed after completion of all feature extractions.")
        .def("jobs_completed", &tp::ThreadPool<Ret, Args...>::jobs_completed, "Returns true if all jobs have been completed, false otherwise.");
}

template <typename Ret>
static void bind_MPSCQueue(py::module &m, const std::string &type_name)
{
    py::class_<MPSCQueue<tp::result_t<Ret>>>(m, type_name.c_str())
        .def(py::init<std::uint16_t>())
        .def("pop", &MPSCQueue<tp::result_t<Ret>>::pop, "Pop an element from the queue in a synchronized manner.")
        .def("empty", &MPSCQueue<tp::result_t<Ret>>::empty, "Returns true if queue is empty, false otherwise.")
        .def("done", &MPSCQueue<tp::result_t<Ret>>::done, "Returns true if producers are done, false otherwise.");
}

PYBIND11_MODULE(gbdc, m)
{
    bind_threadpool<std::vector<std::tuple<std::string, std::string, std::variant<float, int, std::string>>>, std::string, std::string, std::unordered_map<std::string, size_t>>(m, "ThreadPool_Compute");
    m.def("extract_base_features", py::overload_cast<const std::string, const size_t, const size_t>(&extract_features<CNF::BaseFeatures>), "Extract base features", py::arg("filepath"), py::arg("rlim"), py::arg("mlim"));
    m.def("extract_base_features", py::overload_cast<const std::string>(&extract_features<CNF::BaseFeatures>), "Extract base features", py::arg("filepath"));
    m.def("extract_wcnf_base_features", py::overload_cast<const std::string, const size_t, const size_t>(&extract_features<WCNF::BaseFeatures>), "Extract wcnf base features", py::arg("filepath"), py::arg("rlim"), py::arg("mlim"));
    m.def("extract_wcnf_base_features", py::overload_cast<const std::string>(&extract_features<WCNF::BaseFeatures>), "Extract wcnf base features", py::arg("filepath"));
    m.def("extract_opb_base_features", py::overload_cast<const std::string, const size_t, const size_t>(&extract_features<OPB::BaseFeatures>), "Extract opb base features", py::arg("filepath"), py::arg("rlim"), py::arg("mlim"));
    m.def("extract_opb_base_features", py::overload_cast<const std::string>(&extract_features<OPB::BaseFeatures>), "Extract opb base features", py::arg("filepath"));
    m.def("extract_gate_features", py::overload_cast<const std::string, const size_t, const size_t>(&extract_features<CNFGateFeatures>), "Extract gate features", py::arg("filepath"), py::arg("rlim"), py::arg("mlim"));
    m.def("extract_gate_features", py::overload_cast<const std::string>(&extract_features<CNFGateFeatures>), "Extract gate features", py::arg("filepath"));
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
