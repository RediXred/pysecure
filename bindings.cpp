#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "db.h"

namespace py = pybind11;

PYBIND11_MODULE(analyzer, m) {
    py::class_<File>(m, "File")
        .def_readonly("id", &File::id)
        .def_readonly("path", &File::path);

    py::class_<Import>(m, "Import")
        .def_readonly("file_id", &Import::file_id)
        .def_readonly("module", &Import::module)
        .def_readonly("name", &Import::name);
    
    py::class_<DangerousCall>(m, "DangerousCall")
        .def_readonly("function", &DangerousCall::function)
        .def_readonly("from", &DangerousCall::from)
        .def_readonly("line", &DangerousCall::line)
        .def_readonly("file", &DangerousCall::file);

    py::class_<DB, std::shared_ptr<DB>>(m, "DB")
        .def(py::init<const std::string&>())
        .def("files", &DB::files)
        .def("add_file", &DB::add_file)
        .def("create_graph", &DB::create_graph)
        .def("create_call_graph", &DB::create_call_graph)
        .def("ents", &DB::ents, py::arg("filename"), py::arg("include_builtin") = true)
        .def("get_all_imports", &DB::get_all_imports)
        .def("get_dangerous", &DB::get_dangerous);



    m.def("open", [](const std::string& path) {
        return std::make_shared<DB>(path);
    });
}
