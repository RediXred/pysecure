#include <Python.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include "db.h"
#include <sqlite3.h>
#include <functional>
#include <set>
#include <deque>
#include <unordered_map>
#include <functional>
#include <frameobject.h>
#include <sys/resource.h>
#include <chrono>
#include <iomanip>


enum class Pass {
    DECLARATIONS,
    REFERENCES
};


static std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> g_function_start_times;
static bool g_time_profiling = false;

std::string get_call_name(PyObject* func);
std::string expr_to_str(PyObject* node);
std::string extract_call_args(PyObject* call);

static PyObject* g_ast_module = nullptr;
static PyObject* g_current_source = nullptr;


namespace fs = std::filesystem;

static std::unordered_set<std::string> g_hooked_funcs;

static inline bool is_internal_filename(const char* filename) {
    if (!filename) return true;
    return filename[0] == '<';
}

int trace_func(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg) {
    if (!frame) return 0;
    
    PyCodeObject* code = PyFrame_GetCode(frame); //байткод из фрейма
    if (!code) return 0;

    //имя функции и файл
    PyObject* name_obj = PyObject_GetAttrString((PyObject*)code, "co_name");
    PyObject* file_obj = PyObject_GetAttrString((PyObject*)code, "co_filename");

    const char* func_name_c = nullptr;
    const char* filename_c = nullptr;

    if (name_obj && PyUnicode_Check(name_obj))
        func_name_c = PyUnicode_AsUTF8(name_obj);
    if (file_obj && PyUnicode_Check(file_obj))
        filename_c = PyUnicode_AsUTF8(file_obj);

    // фильтр внутренних файлов
    if (is_internal_filename(filename_c)) {
        Py_XDECREF(name_obj);
        Py_XDECREF(file_obj);
        return 0;
    }
    // имя модуля текущего контекста
    const char* module_c = nullptr;
    PyObject* globals = PyFrame_GetGlobals(frame);
    if (globals) {
        PyObject* modname = PyDict_GetItemString(globals, "__name__");
        if (modname && PyUnicode_Check(modname))
            module_c = PyUnicode_AsUTF8(modname);
    }

    std::string func_name = func_name_c ? func_name_c : "";
    std::string qualified = (module_c && module_c[0] != '\0') ? std::string(module_c) + "." + func_name : func_name;

    bool is_hooked = false;
    bool should_profile = false;
    
    if (!func_name.empty()) {
        if (g_hooked_funcs.count(func_name)) is_hooked = true;
        if (!is_hooked && g_hooked_funcs.count(qualified)) is_hooked = true;
        
        should_profile = is_hooked && g_time_profiling;
    }

    if (!g_time_profiling && !is_hooked) {
        Py_XDECREF(name_obj);
        Py_XDECREF(file_obj);
        return 0;
    }
    if ((g_time_profiling) && !is_hooked) {
        Py_XDECREF(name_obj);
        Py_XDECREF(file_obj);
        return 0;
    }



    if (g_time_profiling) {
        if (what != PyTrace_CALL && what != PyTrace_RETURN) {
            Py_XDECREF(name_obj);
            Py_XDECREF(file_obj);
            return 0;
        }
    } else {
        if (what != PyTrace_CALL) {
            Py_XDECREF(name_obj);
            Py_XDECREF(file_obj);
            return 0;
        }
    }

    int line = PyFrame_GetLineNumber(frame);
    
    //профилирование
    if (g_time_profiling && is_hooked) {
        if (what == PyTrace_CALL) {
            g_function_start_times[qualified] = std::chrono::high_resolution_clock::now();
            
            std::cout << "\n[" << qualified << "] STARTED at line " << line << " in " << (filename_c ? filename_c : "<unknown>") << std::endl;
        }
        else if (what == PyTrace_RETURN) {
            auto it = g_function_start_times.find(qualified);
            if (it != g_function_start_times.end()) {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - it->second);
                double duration_ms = duration.count() / 1000.0;
                
                std::cout << "[" << qualified << "] FINISHED in " << std::fixed << std::setprecision(3) << duration_ms << " ms" << std::endl;
                
                g_function_start_times.erase(it);
            }
        }
    }

    //хуки
    if (what == PyTrace_CALL && is_hooked) {
        std::cout << "\nHOOKED: " << qualified << " (Line: " << line << " in " << (filename_c ? filename_c : "<unknown>") << ")" << std::endl;

        //аргументы
        PyObject* locals = PyFrame_GetLocals(frame);
        if (locals && PyDict_Check(locals)) {
            int argcount = code->co_argcount;
            int kwonly = code->co_kwonlyargcount;
            int total = argcount + kwonly;

            PyObject* varnames = PyObject_GetAttrString((PyObject*)code, "co_varnames");

            if (varnames && PyTuple_Check(varnames)) {
                int arg_count = 0;
                std::cout << "   Args (" << total << " total):" << std::endl;
                
                for (int i = 0; i < total; i++) {
                    PyObject* name_o = PyTuple_GetItem(varnames, i);
                    if (!name_o || !PyUnicode_Check(name_o)) continue;

                    PyObject* value = PyDict_GetItem(locals, name_o);
                    if (!value) continue;
                    const char* name = PyUnicode_AsUTF8(name_o);
                    arg_count++;

                    std::string value_str;
                    PyObject* repr = PyObject_Repr(value);
                    if (repr) {
                        const char* repr_c = PyUnicode_AsUTF8(repr);
                        value_str = repr_c ? std::string(repr_c) : "<repr?>";
                        if (value_str.length() > 60) {
                            value_str = value_str.substr(0, 57) + "...";
                        }
                        Py_DECREF(repr);
                    } else {
                        value_str = "<repr-error>";
                    }
                    std::cout << name << ": " << value_str << std::endl;
                }
            }
            Py_XDECREF(varnames);
        }
        Py_XDECREF(locals);
        
        std::cout << std::endl;
    }

    Py_XDECREF(name_obj);
    Py_XDECREF(file_obj);
    return 0;
}




bool create_project_db(const std::string& db_path) {
    sqlite3* db;
    if (sqlite3_open(db_path.c_str(), &db)) {
        std::cerr << "Cannot create database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    const char* sql = R"(
    CREATE TABLE IF NOT EXISTS files(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        path TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS classes(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT,
        file_id INTEGER,
        start_line INTEGER,
        end_line INTEGER
    );
    CREATE TABLE IF NOT EXISTS functions(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT,
        class_id INTEGER,
        file_id INTEGER,
        start_line INTEGER,
        end_line INTEGER,
        args TEXT
    );
    CREATE TABLE IF NOT EXISTS refs(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        from_id INTEGER,
        to_id INTEGER,
        kind TEXT,
        args TEXT
    );
        CREATE TABLE IF NOT EXISTS imports(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        file_id INTEGER,
        module TEXT,
        name TEXT
    );
    )";

    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

std::string read_python_file(const std::string& path) {
    std::ifstream infile(path, std::ios::binary);
    if (!infile.is_open()) return "";

    std::stringstream buffer;
    buffer << infile.rdbuf();
    std::string source = buffer.str();
    // удаление bom
    if (source.size() >= 3 &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        source = source.substr(3);
    }

    return source;
}

std::vector<std::string> scan_source_files(const std::string& project_path) {
    std::vector<std::string> files;
    for (auto& p : fs::recursive_directory_iterator(project_path)) {
        if (p.is_regular_file()) {
            std::string ext = p.path().extension().string();
            if (ext == ".py") {
                files.push_back(p.path().string());
            }
        }
    }
    return files;
}

PyObject* get_attr(PyObject* obj, const char* attr) {
    PyObject* value = PyObject_GetAttrString(obj, attr);
    if (!value) PyErr_Clear();
    return value;
}


std::string safe_unparse(PyObject* node) {
    //читаемое представление ast-узла (fallback)
    if (!node) return "<expr>";

    //если это Name (переменная) или Attribute (obj.method)
    if (PyObject_HasAttrString(node, "id")) { // ast.Name(id=...)
        PyObject* id = get_attr(node, "id");
        if (id) {
            const char* s = PyUnicode_AsUTF8(id);
            return s ? std::string(s) : "<expr>";
        }
    }
    if (PyObject_HasAttrString(node, "attr") && PyObject_HasAttrString(node, "value")) { // ast.Attribute
        PyObject* value = get_attr(node, "value"); // Name(id='obj')
        PyObject* attr = get_attr(node, "attr"); // method
        std::string left = expr_to_str(value); // для случаев obj1.obj2...attr
        const char* an = attr ? PyUnicode_AsUTF8(attr) : nullptr;
        return left + "." + (an ? an : "<expr>");
    }

    //ast.get_source_segment(g_current_source, node)
    if (g_ast_module && g_current_source) {
        PyObject* get_src = PyObject_GetAttrString(g_ast_module, "get_source_segment"); // ast.get_source_segment извлекает конкретный фрагмент кода по узлу
        if (get_src && PyCallable_Check(get_src)) {
            PyObject* res = PyObject_CallFunctionObjArgs(get_src, g_current_source, node, nullptr);
            Py_XDECREF(get_src);
            if (res) {
                if (res != Py_None) {
                    const char* s = PyUnicode_AsUTF8(res);
                    std::string out = s ? s : "<expr>";
                    Py_DECREF(res);
                    return out;
                }
                Py_DECREF(res);
            }
            PyErr_Clear();
        } else {
            Py_XDECREF(get_src);
        }
    }

    
    if (g_ast_module) {
        // ast.unparse(node), генерит код из AST
        PyObject* unparse = PyObject_GetAttrString(g_ast_module, "unparse");
        if (unparse && PyCallable_Check(unparse)) {
            PyObject* res = PyObject_CallFunctionObjArgs(unparse, node, nullptr);
            Py_XDECREF(unparse);
            if (res) {
                const char* s = PyUnicode_AsUTF8(res);
                std::string out = s ? s : "<expr>";
                Py_DECREF(res);
                if (out.find("<ast.") != std::string::npos) return "<expr>";
                return out;
            }
            PyErr_Clear();
        } else {
            Py_XDECREF(unparse);
        }

        //ast.dump(node), дампит структуру ast
        PyObject* dump = PyObject_GetAttrString(g_ast_module, "dump");
        if (dump && PyCallable_Check(dump)) {
            PyObject* res = PyObject_CallFunctionObjArgs(dump, node, nullptr);
            Py_XDECREF(dump);
            if (res) {
                const char* s = PyUnicode_AsUTF8(res);
                std::string out = s ? s : "<expr>";
                Py_DECREF(res);
                if (out.find("<ast.") != std::string::npos) return "<expr>";
                return out;
            }
            PyErr_Clear();
        } else {
            Py_XDECREF(dump);
        }
    }

    //repr(node)
    PyObject* reprobj = PyObject_Repr(node);
    if (reprobj) {
        const char* r = PyUnicode_AsUTF8(reprobj);
        std::string s = r ? r : "<expr>";
        Py_DECREF(reprobj);
        if (s.find("<ast.") != std::string::npos) return "<expr>";
        return s;
    }

    return "<expr>";
}

std::string slice_to_str(PyObject* slice) {
    if (!slice) return "<slice>";

    if (PyObject_HasAttrString(slice, "value")) { // индекс [val]
        PyObject* v = get_attr(slice, "value");
        return expr_to_str(v);
    }
    if (PyObject_HasAttrString(slice, "lower") || PyObject_HasAttrString(slice, "upper")) { // [a:b:c]
        PyObject* lower = get_attr(slice, "lower");
        PyObject* upper = get_attr(slice, "upper");
        PyObject* step = get_attr(slice, "step");
        std::string out;
        out += lower ? expr_to_str(lower) : "";
        out += ":";
        out += upper ? expr_to_str(upper) : "";
        if (step) out += ":" + expr_to_str(step);
        return out;
    }
    // fallback
    return expr_to_str(slice);
}

std::string expr_to_str(PyObject* node) {
    //ast узел в читаемый вид
    if (!node) return "<expr>";

    if (PyLong_Check(node)) return std::to_string(PyLong_AsLong(node));
    if (PyFloat_Check(node)) return std::to_string(PyFloat_AsDouble(node));
    if (PyUnicode_Check(node)) {
        const char* s = PyUnicode_AsUTF8(node);
        return s ? std::string("\"") + s + "\"" : "<expr>";
    }
    if (node == Py_True) return "True";
    if (node == Py_False) return "False";
    if (node == Py_None) return "None";

    //ast.Constant
    if (PyObject_HasAttrString(node, "value")) {
        PyObject* value = get_attr(node, "value");
        if (value) {
            if (PyUnicode_Check(value)) {
                const char* s = PyUnicode_AsUTF8(value);
                return s ? std::string("\"") + s + "\"" : "<expr>";
            }
            if (PyLong_Check(value)) return std::to_string(PyLong_AsLong(value));
            if (PyFloat_Check(value)) return std::to_string(PyFloat_AsDouble(value));
            if (value == Py_True) return "True";
            if (value == Py_False) return "False";
            if (value == Py_None) return "None";
            PyObject* reprobj = PyObject_Repr(value);
            if (reprobj) {
                const char* r = PyUnicode_AsUTF8(reprobj);
                std::string s = r ? r : "<expr>";
                Py_DECREF(reprobj);
                return s;
            }
        }
    }

    //простые идентификаторы ast.Name(id=)
    if (PyObject_HasAttrString(node, "id")) {
        PyObject* id = get_attr(node, "id");
        if (id) {
            const char* tmp = PyUnicode_AsUTF8(id);
            return tmp ? std::string(tmp) : "<expr>";
        }
    }

    //коллекции
    if (PyObject_HasAttrString(node, "elts")) {
        PyObject* elts = get_attr(node, "elts");
        if (elts && PyList_Check(elts)) {
            //определение tuple/list
            if (PyObject_HasAttrString(node, "__class__")) {
                PyObject* cls = get_attr(node, "__class__"); // класс узла
                if (cls) {
                    PyObject* nm = get_attr(cls, "__name__"); // имя класса
                    const char* nm_s = nm ? PyUnicode_AsUTF8(nm) : nullptr;
                    if (nm_s && std::string(nm_s) == "Tuple") {
                        // tuple
                        std::string out = "(";
                        for (Py_ssize_t i = 0; i < PyList_Size(elts); i++) {
                            if (i) out += ", ";
                            out += expr_to_str(PyList_GetItem(elts, i)); //для вложенных
                        }
                        out += ")";
                        return out;
                    }
                }
            }
            // list
            std::string out = "[";
            for (Py_ssize_t i = 0; i < PyList_Size(elts); i++) {
                if (i) out += ", ";
                out += expr_to_str(PyList_GetItem(elts, i));
            }
            out += "]";
            return out;
        }
    }

    // словарь
    if (PyObject_HasAttrString(node, "keys") && PyObject_HasAttrString(node, "values")) {
        PyObject* keys = get_attr(node, "keys");
        PyObject* values = get_attr(node, "values");
        if (keys && values && PyList_Check(keys) && PyList_Check(values)) {
            std::string out = "{";
            Py_ssize_t n = PyList_Size(keys);
            for (Py_ssize_t i = 0; i < n; i++) {
                if (i) out += ", ";
                out += expr_to_str(PyList_GetItem(keys, i)) + ": " + expr_to_str(PyList_GetItem(values, i));
            }
            out += "}";
            return out;
        }
    }

    //вызовы
    if (PyObject_HasAttrString(node, "func")) {
        PyObject* func = get_attr(node, "func");
        if (func) {
            std::string name = get_call_name(func);
            std::string args = extract_call_args(node);
            if (!name.empty()) return name + args;
            // fallback рекурсивно обработка func + аргументы
            return expr_to_str(func) + args;
        }
    }

    //атрибуты объекта
    if (PyObject_HasAttrString(node, "value") && PyObject_HasAttrString(node, "attr")) {
        PyObject* value = get_attr(node, "value");
        PyObject* attr = get_attr(node, "attr");
        const char* an = attr ? PyUnicode_AsUTF8(attr) : nullptr;
        return expr_to_str(value) + "." + (an ? an : "<expr>");
    }

    //срезы
    if (PyObject_HasAttrString(node, "value") && PyObject_HasAttrString(node, "slice")) {
        PyObject* value = get_attr(node, "value");
        PyObject* slice = get_attr(node, "slice");
        return expr_to_str(value) + "[" + slice_to_str(slice) + "]";
    }

    // логические операции
    if (PyObject_HasAttrString(node, "values") && PyObject_HasAttrString(node, "op")) {
        PyObject* vals = get_attr(node, "values"); // список операндов
        PyObject* op = get_attr(node, "op"); // оператор
        const char* opname = nullptr;
        if (op && PyObject_HasAttrString(op, "__class__")) {
            PyObject* cls = get_attr(op, "__class__");
            PyObject* nm = cls ? get_attr(cls, "__name__") : nullptr; // имя оператора
            opname = nm ? PyUnicode_AsUTF8(nm) : nullptr;
        }
        std::string op_s = opname ? (std::string(opname) == "And" ? "and" : (std::string(opname) == "Or" ? "or" : opname)) : "<op>";
        if (vals && PyList_Check(vals)) {
            std::string out;
            for (Py_ssize_t i = 0; i < PyList_Size(vals); i++) {
                if (i) out += " " + op_s + " ";
                out += expr_to_str(PyList_GetItem(vals, i)); // рекурсивно обработка операндов
            }
            return "(" + out + ")";
        }
    }

    // сравнения
    if (PyObject_HasAttrString(node, "left") && PyObject_HasAttrString(node, "ops") && PyObject_HasAttrString(node, "comparators")) {
        PyObject* left = get_attr(node, "left"); // левый операнд
        PyObject* ops = get_attr(node, "ops"); //список операторорв сравнения
        PyObject* comps = get_attr(node, "comparators"); // список правых операндов (один на каждый оператор)
        if (ops && comps && PyList_Check(ops) && PyList_Check(comps)) {
            std::string out = expr_to_str(left);
            Py_ssize_t n = PyList_Size(ops);
            for (Py_ssize_t i = 0; i < n; i++) {
                PyObject* op = PyList_GetItem(ops, i);
                PyObject* comp = PyList_GetItem(comps, i);
                const char* opname = nullptr; // имя оператора (eq, lt..)
                if (op && PyObject_HasAttrString(op, "__class__")) {
                    PyObject* cls = get_attr(op, "__class__");
                    PyObject* nm = cls ? get_attr(cls, "__name__") : nullptr;
                    opname = nm ? PyUnicode_AsUTF8(nm) : nullptr;
                }
                std::string op_s = opname ? std::string(opname) : "<cmp>";
                if (op_s == "Eq") op_s = "==";
                else if (op_s == "NotEq") op_s = "!=";
                else if (op_s == "Lt") op_s = "<";
                else if (op_s == "LtE") op_s = "<=";
                else if (op_s == "Gt") op_s = ">";
                else if (op_s == "GtE") op_s = ">=";
                out += " " + op_s + " " + expr_to_str(comp);
            }
            return "(" + out + ")";
        }
    }

    // унарные операторы
    if (PyObject_HasAttrString(node, "operand") && PyObject_HasAttrString(node, "op")) {
        PyObject* op = get_attr(node, "op");
        PyObject* operand = get_attr(node, "operand");
        const char* opname = nullptr;
        if (op && PyObject_HasAttrString(op, "__class__")) {
            PyObject* cls = get_attr(op, "__class__");
            PyObject* nm = cls ? get_attr(cls, "__name__") : nullptr;
            opname = nm ? PyUnicode_AsUTF8(nm) : nullptr;
        }
        std::string op_s = opname ? std::string(opname) : "<uop>";
        if (op_s == "Not") op_s = "not ";
        else if (op_s == "USub") op_s = "-";
        else if (op_s == "UAdd") op_s = "+";
        return op_s + expr_to_str(operand);
    }

    // тернарные выражения x if cond else...
    if (PyObject_HasAttrString(node, "test") && PyObject_HasAttrString(node, "body") && PyObject_HasAttrString(node, "orelse")) {
        PyObject* test = get_attr(node, "test"); // условие
        PyObject* body = get_attr(node, "body"); // тело (true)
        PyObject* orelse = get_attr(node, "orelse"); // false
        return "(" + expr_to_str(body) + " if " + expr_to_str(test) + " else " + expr_to_str(orelse) + ")";
    }

    // lambda/gen-exps
    if (PyObject_HasAttrString(node, "args") && PyObject_HasAttrString(node, "body")) return "<lambda>";
    if (PyObject_HasAttrString(node, "generators")) return "<comprehension>";

    // fallback
    return safe_unparse(node);
}




std::string extract_function_args(PyObject* func_node) {
    PyObject* args = get_attr(func_node, "args");
    if (!args) return "";

    std::vector<std::string> result;

    PyObject* py_args = get_attr(args, "args");
    if (py_args && PyList_Check(py_args)) {
        Py_ssize_t total = PyList_Size(py_args);

        PyObject* defaults = get_attr(args, "defaults");
        Py_ssize_t defaults_count = defaults ? PyList_Size(defaults) : 0;
        Py_ssize_t no_default = total - defaults_count;

        for (Py_ssize_t i = 0; i < total; i++) {
            PyObject* arg = PyList_GetItem(py_args, i);
            PyObject* name = get_attr(arg, "arg");
            if (!name) continue;

            std::string arg_name = PyUnicode_AsUTF8(name);

            if (i >= no_default) {
                arg_name += "=...";
            }

            result.push_back(arg_name);
        }
    }
    //*args
    PyObject* vararg = get_attr(args, "vararg");
    if (vararg) {
        PyObject* name = get_attr(vararg, "arg");
        if (name)
            result.push_back("*" + std::string(PyUnicode_AsUTF8(name)));
    }
    // **kwargs
    PyObject* kwarg = get_attr(args, "kwarg");
    if (kwarg) {
        PyObject* name = get_attr(kwarg, "arg");
        if (name)
            result.push_back("**" + std::string(PyUnicode_AsUTF8(name)));
    }

    std::string out = "(";
    for (size_t i = 0; i < result.size(); i++) {
        out += result[i];
        if (i + 1 < result.size())
            out += ", ";
    }
    out += ")";
    return out;
}

std::string extract_call_args(PyObject* call) {
    if (!call) return "";

    std::vector<std::string> args;

    PyObject* py_args = get_attr(call, "args");
    if (py_args && PyList_Check(py_args)) {
        for (Py_ssize_t i = 0; i < PyList_Size(py_args); i++) {
            PyObject* item = PyList_GetItem(py_args, i);
            args.push_back(expr_to_str(item));
        }
    }

    PyObject* kwargs = get_attr(call, "keywords");
    if (kwargs && PyList_Check(kwargs)) {
        for (Py_ssize_t i = 0; i < PyList_Size(kwargs); i++) {
            PyObject* kw = PyList_GetItem(kwargs, i);
            PyObject* arg = get_attr(kw, "arg");
            PyObject* val = get_attr(kw, "value");
            if (arg && val) {
                const char* cname = PyUnicode_AsUTF8(arg);
                if (cname)
                    args.push_back(std::string(cname) + "=" + expr_to_str(val));
            }
        }
    }

    std::string res = "(";
    for (size_t i = 0; i < args.size(); i++) {
        res += args[i];
        if (i + 1 < args.size()) res += ", ";
    }
    res += ")";
    return res;
}


std::string get_call_name(PyObject* func) {
    if (!func) return "";

    // obj.method()
    if (PyObject_HasAttrString(func, "attr") && PyObject_HasAttrString(func, "value")) {
        PyObject* attr = get_attr(func, "attr");
        PyObject* value = get_attr(func, "value");
        if (attr && value && PyObject_HasAttrString(value, "id")) {
            PyObject* id = get_attr(value, "id");
            if (id) {
                const char* cid = PyUnicode_AsUTF8(id);
                const char* cname = PyUnicode_AsUTF8(attr);
                if (cid && cname) return cname;
            }
        }
    }

    //func()
    if (PyObject_HasAttrString(func, "id")) {
        PyObject* id = get_attr(func, "id");
        if (id) {
            const char* cname = PyUnicode_AsUTF8(id);
            if (cname) return cname;
        }
    }

    return "";
}

PyObject* extract_call(PyObject* node, PyObject* CallType, PyObject* AwaitType) {
    if (!node) return nullptr;
    if (PyObject_IsInstance(node, CallType)) return node;

    if (PyObject_IsInstance(node, AwaitType)) {
        PyObject* value = get_attr(node, "value");
        if (value && PyObject_IsInstance(value, CallType))
            return value;
    }

    return nullptr;
}


int resolve_node_type(PyObject* node, PyObject* ast_module, const std::vector<int>& class_stack, const std::vector<std::unordered_map<std::string, int>>& var_type_stack, std::unordered_map<int, std::unordered_map<std::string, int>>& class_attr_types) {
    if (!node || node == Py_None) return 0;

    //простые имена (self, x..)
    if (PyObject_HasAttrString(node, "id")) {
        PyObject* py_id = PyObject_GetAttrString(node, "id");
        if (py_id) {
            const char* name_c = PyUnicode_AsUTF8(py_id);
            std::string name = name_c ? name_c : "";
            Py_DECREF(py_id);

            if (name == "self" && !class_stack.empty()) return class_stack.back(); //если self то id текущего класса
            if (!var_type_stack.empty()) {
                auto it = var_type_stack.back().find(name);
                if (it != var_type_stack.back().end()) return it->second;
            }
        }
    }

    // атрибуты рекурсивно (self.x, obj1.obj2..attr)
    if (PyObject_HasAttrString(node, "attr") && PyObject_HasAttrString(node, "value")) {
        PyObject* value_node = PyObject_GetAttrString(node, "value");
        PyObject* attr_node = PyObject_GetAttrString(node, "attr");

        std::string attr_name;
        if (attr_node) {
            const char* a = PyUnicode_AsUTF8(attr_node);
            attr_name = a ? a : "";
        }

        int parent_type = resolve_node_type(value_node, ast_module, class_stack, var_type_stack, class_attr_types);

        Py_XDECREF(value_node);
        Py_XDECREF(attr_node);

        if (parent_type != 0) {
            auto cit = class_attr_types.find(parent_type);
            if (cit != class_attr_types.end()) {
                auto ait = cit->second.find(attr_name);
                if (ait != cit->second.end()) return ait->second;
            }
        }
    }

    return 0;
}


void parse_project(DB& db, const std::vector<File>& files, Pass pass) {
    Py_Initialize();
    PyObject* ast = PyImport_ImportModule("ast");
    if (!ast) { PyErr_Print(); return; }
    
    // Типы узлов
    PyObject* ClassDefType = PyObject_GetAttrString(ast, "ClassDef");
    PyObject* FunctionDefType = PyObject_GetAttrString(ast, "FunctionDef");
    PyObject* AsyncFunctionDefType = PyObject_GetAttrString(ast, "AsyncFunctionDef");
    PyObject* CallType = PyObject_GetAttrString(ast, "Call");
    PyObject* AwaitType = PyObject_GetAttrString(ast, "Await");
    PyObject* AssignType = PyObject_GetAttrString(ast, "Assign");
    PyObject* AnnAssignType = PyObject_GetAttrString(ast, "AnnAssign");
    PyObject* IfExpType = PyObject_GetAttrString(ast, "IfExp");
    PyObject* iter_children = PyObject_GetAttrString(ast, "iter_child_nodes");
    PyObject* builtins_mod = PyImport_ImportModule("builtins");

    std::vector<std::unordered_map<std::string, int>> var_type_stack; //типы локальных переменных по областям видимости
    std::unordered_map<int, std::unordered_map<std::string, int>> class_attr_types; // class_id: {attr_name: type_id}, типы атрибутов классов

    for (const auto& f : files) {
        std::string source = read_python_file(f.path);
        if (source.empty()) continue;

        PyObject* tree = PyObject_CallMethod(ast, "parse", "s", source.c_str());
        if (!tree) { PyErr_Clear(); continue; }

        std::vector<int> class_stack;
        std::vector<int> function_stack;

        std::function<void(PyObject*)> walk = [&](PyObject* node) {
            if (!node || node == Py_None) return;

            //pass1
            if (pass == Pass::DECLARATIONS) {
                if (PyObject_IsInstance(node, ClassDefType)) {
                    PyObject* name = PyObject_GetAttrString(node, "name");
                    if (name) {
                        PyObject* lineno_obj = PyObject_GetAttrString(node, "lineno");
                        PyObject* end_lineno_obj = PyObject_GetAttrString(node, "end_lineno");

                        int start_line = lineno_obj ? (int)PyLong_AsLong(lineno_obj) : 0;
                        int end_line = end_lineno_obj ? (int)PyLong_AsLong(end_lineno_obj) : start_line;

                        db.add_class(PyUnicode_AsUTF8(name), f.id, start_line, end_line);

                        class_stack.push_back(db.last_insert_id());
                        Py_DECREF(name);
                    }
                }
                if (PyObject_IsInstance(node, FunctionDefType) || PyObject_IsInstance(node, AsyncFunctionDefType)) {
                    PyObject* name = PyObject_GetAttrString(node, "name");
                    if (name) {
                        int class_id = class_stack.empty() ? 0 : class_stack.back();
                        PyObject* lineno_obj = PyObject_GetAttrString(node, "lineno");
                        PyObject* end_lineno_obj = PyObject_GetAttrString(node, "end_lineno");

                        int start_line = lineno_obj ? (int)PyLong_AsLong(lineno_obj) : 0;
                        int end_line = end_lineno_obj ? (int)PyLong_AsLong(end_lineno_obj) : start_line;

                        Py_XDECREF(lineno_obj);
                        Py_XDECREF(end_lineno_obj);

                        db.add_function(PyUnicode_AsUTF8(name), f.id, class_id, start_line, end_line, "");
                        function_stack.push_back(db.last_insert_id());
                        Py_DECREF(name);
                    }
                }
            }

            //pass2
            if (pass == Pass::REFERENCES) {
                if (PyObject_IsInstance(node, ClassDefType)) {
                    PyObject* name_obj = PyObject_GetAttrString(node, "name");
                    std::string class_name;
                    if (name_obj) {
                        const char* cn = PyUnicode_AsUTF8(name_obj);
                        class_name = cn ? cn : "";
                        Py_DECREF(name_obj);
                    }
                    int child_class_id = db.get_class_id_by_name(class_name);
                    
                    // список базовых классов (наследование клссов)
                    PyObject* bases = PyObject_GetAttrString(node, "bases");
                    if (bases && PyList_Check(bases)) {
                        Py_ssize_t n = PyList_Size(bases);
                        for (Py_ssize_t i = 0; i < n; i++) {
                            PyObject* base_node = PyList_GetItem(bases, i);
                            if (!base_node) continue;
                            
                            if (PyObject_HasAttrString(base_node, "id")) {
                                PyObject* base_id_obj = PyObject_GetAttrString(base_node, "id");
                                if (base_id_obj) {
                                    const char* parent_name_c = PyUnicode_AsUTF8(base_id_obj);
                                    std::string parent_name = parent_name_c ? parent_name_c : "";
                                    int parent_class_id = db.get_class_id_by_name(parent_name);
                                    if (parent_class_id != 0) {
                                        db.add_reference(child_class_id, parent_class_id, "inherit", "");
                                    }
                                    Py_DECREF(base_id_obj);
                                }
                            }
                        }
                    }
                    Py_XDECREF(bases);
                    
                    class_stack.push_back(child_class_id);
                }
                if (PyObject_IsInstance(node, FunctionDefType) || PyObject_IsInstance(node, AsyncFunctionDefType)) {
                    PyObject* name = PyObject_GetAttrString(node, "name");
                    if (name) {
                        int class_id = class_stack.empty() ? 0 : class_stack.back();
                        function_stack.push_back(db.get_function_id_by_name_class(PyUnicode_AsUTF8(name), class_id));
                        var_type_stack.emplace_back();
                        Py_DECREF(name);
                    } else {
                        function_stack.push_back(0);
                        var_type_stack.emplace_back();
                    }
                }

                //обработка присваиваний и аннотаций
                bool is_assign = PyObject_IsInstance(node, AssignType);
                bool is_ann_assign = PyObject_IsInstance(node, AnnAssignType);
                
                if (is_assign || is_ann_assign) {
                    PyObject* value = PyObject_GetAttrString(node, "value");
                    int inferred_type = 0;

                    //если справа присваивания вызов (ctor)
                    if (value && PyObject_IsInstance(value, CallType)) {
                        PyObject* func = PyObject_GetAttrString(value, "func"); //что вызвалось
                        if (func) {
                            if (PyObject_HasAttrString(func, "id")) {
                                PyObject* id = PyObject_GetAttrString(func, "id");
                                if (id) {
                                    const char* cname = PyUnicode_AsUTF8(id);
                                    if (cname) inferred_type = db.get_class_id_by_name(cname);
                                    Py_DECREF(id);
                                }
                            }
                            //func.attr
                            if (inferred_type == 0 && PyObject_HasAttrString(func, "attr")) {
                                PyObject* attr = PyObject_GetAttrString(func, "attr");
                                if (attr) {
                                    const char* aname = PyUnicode_AsUTF8(attr);
                                    if (aname) inferred_type = db.get_class_id_by_name(aname);
                                    Py_DECREF(attr);
                                }
                            }
                            Py_DECREF(func);
                        }
                    }

                    // value - ifExp
                    if (value && inferred_type == 0 && PyObject_HasAttrString(value, "body") && PyObject_HasAttrString(value, "orelse")) {
                        PyObject* body = PyObject_GetAttrString(value, "body");
                        PyObject* orelse = PyObject_GetAttrString(value, "orelse");

                        //в body - calltype
                        if (body && PyObject_IsInstance(body, CallType)) {
                            PyObject* func = PyObject_GetAttrString(body, "func");
                            if (func) {
                                if (PyObject_HasAttrString(func, "id")) {
                                    PyObject* id = PyObject_GetAttrString(func, "id");
                                    if (id) { const char* s = PyUnicode_AsUTF8(id); if (s) inferred_type = db.get_class_id_by_name(s); Py_DECREF(id); }
                                }
                                if (inferred_type == 0 && PyObject_HasAttrString(func, "attr")) {
                                    PyObject* attr = PyObject_GetAttrString(func, "attr");
                                    if (attr) { const char* s = PyUnicode_AsUTF8(attr); if (s) inferred_type = db.get_class_id_by_name(s); Py_DECREF(attr); }
                                }
                                Py_DECREF(func);
                            }
                        }

                        //в orelse - calltype
                        if (inferred_type == 0 && orelse && PyObject_IsInstance(orelse, CallType)) {
                            PyObject* func = PyObject_GetAttrString(orelse, "func");
                            if (func) {
                                if (PyObject_HasAttrString(func, "id")) {
                                    PyObject* id = PyObject_GetAttrString(func, "id");
                                    if (id) { const char* s = PyUnicode_AsUTF8(id); if (s) inferred_type = db.get_class_id_by_name(s); Py_DECREF(id); }
                                }
                                if (inferred_type == 0 && PyObject_HasAttrString(func, "attr")) {
                                    PyObject* attr = PyObject_GetAttrString(func, "attr");
                                    if (attr) { const char* s = PyUnicode_AsUTF8(attr); if (s) inferred_type = db.get_class_id_by_name(s); Py_DECREF(attr); }
                                }
                                Py_DECREF(func);
                            }
                        }

                        Py_XDECREF(body);
                        Py_XDECREF(orelse);
                    }

                    //если аннотированное присваивание, r: Re = Re()
                    if (is_ann_assign && inferred_type == 0) {
                        PyObject* ann = PyObject_GetAttrString(node, "annotation");
                        if (ann) {
                            if (PyObject_HasAttrString(ann, "id")) {
                                PyObject* id = PyObject_GetAttrString(ann, "id");
                                if (id) { const char* s = PyUnicode_AsUTF8(id); if (s) inferred_type = db.get_class_id_by_name(s); Py_DECREF(id); }
                            }
                            Py_XDECREF(ann);
                        }
                    }

                    //сохраняем тип в контексте
                    if (inferred_type) {
                        PyObject* targets = is_assign ? PyObject_GetAttrString(node, "targets") : nullptr; //левое значение присв.
                        PyObject* target = nullptr;
                        if (is_assign && targets && PyList_Check(targets) && PyList_Size(targets) > 0) {
                            target = PyList_GetItem(targets, 0); //правое значение
                        } else if (is_ann_assign) {
                            target = PyObject_GetAttrString(node, "target");
                        }

                        if (target) {
                            //простое имя (Constr())
                            if (PyObject_HasAttrString(target, "id")) {
                                PyObject* id_obj = PyObject_GetAttrString(target, "id");
                                if (id_obj) {
                                    const char* vname = PyUnicode_AsUTF8(id_obj);
                                    if (vname && !var_type_stack.empty()) var_type_stack.back()[vname] = inferred_type; //запоминаем тип переменной в контексте функции
                                    Py_DECREF(id_obj);
                                }
                            }
                            // self.attr = ..
                            else if (PyObject_HasAttrString(target, "attr")) {
                                PyObject* val_node = PyObject_GetAttrString(target, "value");
                                if (val_node) {
                                    if (PyObject_HasAttrString(val_node, "id")) {
                                        PyObject* id_obj = PyObject_GetAttrString(val_node, "id");
                                        if (id_obj) {
                                            const char* vname = PyUnicode_AsUTF8(id_obj);
                                            if (vname && std::string(vname) == "self" && !class_stack.empty()) { //атрибут класса
                                                //ex: class S: self.repo = Repo()
                                                //ex: class_attr_types[S_id]["repo"] = Repo_id
                                                PyObject* aname_obj = PyObject_GetAttrString(target, "attr");
                                                if (aname_obj) {
                                                    const char* aname = PyUnicode_AsUTF8(aname_obj);
                                                    if (aname) class_attr_types[class_stack.back()][aname] = inferred_type;
                                                    Py_DECREF(aname_obj);
                                                }
                                            }
                                            Py_DECREF(id_obj);
                                        }
                                    }
                                    Py_DECREF(val_node);
                                }
                            }

                            if (is_ann_assign && target) {
                                if (!is_assign && target) Py_XDECREF(target);
                            }
                        }
                        if (targets) Py_DECREF(targets);
                    }

                    Py_XDECREF(value);
                }

                //вызовы
                PyObject* call = extract_call(node, CallType, AwaitType);
                if (call) {
                    int from_id = function_stack.empty() ? 0 : function_stack.back();
                    PyObject* func = PyObject_GetAttrString(call, "func");
                    std::string argsc = extract_call_args(call);

                    if (func) {
                        if (PyObject_HasAttrString(func, "attr")) { //obj.method()
                            PyObject* target_node = PyObject_GetAttrString(func, "value");
                            PyObject* attr_node = PyObject_GetAttrString(func, "attr");
                            std::string method_name;
                            if (attr_node) {
                                const char* mn = PyUnicode_AsUTF8(attr_node);
                                method_name = mn ? mn : "";
                            }
                            //определение класса объекта
                            //ex:self.repo.method() -> ret Repo_id
                            int target_class_id = resolve_node_type(target_node, ast, class_stack, var_type_stack, class_attr_types);
                            if (target_class_id) {
                                int to_id = db.get_function_id_by_name_class(method_name, target_class_id);
                                if (to_id) db.add_reference(from_id, to_id, "call", argsc);
                            }
                            Py_XDECREF(target_node);
                            Py_XDECREF(attr_node);
                        } else if (PyObject_HasAttrString(func, "id")) { //func()
                            PyObject* id_obj = PyObject_GetAttrString(func, "id");
                            if (id_obj) {
                                const char* name = PyUnicode_AsUTF8(id_obj);
                                if (name) {
                                    int to_id = db.get_function_id_by_name(name);
                                    if (to_id) {
                                        db.add_reference(from_id, to_id, "call", argsc);
                                    } else {
                                        int cid = db.get_class_id_by_name(name);
                                        if (cid) {
                                            db.add_reference(from_id, cid, "instantiate", argsc);
                                        } else {
                                            // проверка в __builtins__
                                            if (builtins_mod && PyObject_HasAttrString(builtins_mod, name)) {
                                                db.add_reference(from_id, 0, "call_builtin:" + std::string(name), argsc);
                                            }
                                        }
                                    }

                                }
                                Py_DECREF(id_obj);
                            }
                        }
                        Py_DECREF(func);
                    }
                }
            }

            //dfs
            PyObject* children = PyObject_CallFunctionObjArgs(iter_children, node, nullptr);
            if (children) {
                PyObject* it = PyObject_GetIter(children);
                PyObject* ch;
                while ((ch = PyIter_Next(it))) {
                    walk(ch);
                    Py_DECREF(ch);
                }
                Py_DECREF(it);
                Py_DECREF(children);
            }

            //pop ctx
            if (PyObject_IsInstance(node, ClassDefType) && !class_stack.empty()) class_stack.pop_back();
            if ((PyObject_IsInstance(node, FunctionDefType) || PyObject_IsInstance(node, AsyncFunctionDefType)) && !function_stack.empty()) {
                function_stack.pop_back();
                if (pass == Pass::REFERENCES) var_type_stack.pop_back();
            }
        };

        walk(tree);
        Py_DECREF(tree);
    }

    Py_XDECREF(ClassDefType); Py_XDECREF(FunctionDefType); Py_XDECREF(AsyncFunctionDefType);
    Py_XDECREF(CallType); Py_XDECREF(AwaitType); Py_XDECREF(AssignType); Py_XDECREF(AnnAssignType);
    Py_XDECREF(IfExpType); Py_XDECREF(iter_children); Py_XDECREF(builtins_mod); Py_XDECREF(ast);
    Py_Finalize();
}



int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "few args\n";
        return 1;
    }

    std::string option = argv[1];

    if (option == "--create-db") {
        if (argc < 3) {
            std::cerr << "put folder project\n";
            return 1;
        }

        std::string project_path = argv[2];
        std::string project_name = fs::path(project_path).filename().string();
        std::string db_path = project_name + ".myund";

        if (!create_project_db(db_path)) {
            std::cerr << "Failed to create project database\n";
            return 1;
        }

        DB db(db_path);

        std::vector<std::string> paths = scan_source_files(project_path);
        for (const auto& p : paths) db.add_file(p);

        std::vector<File> files = db.files();

        parse_project(db, files, Pass::DECLARATIONS);
        parse_project(db, files, Pass::REFERENCES);

        std::cout << "Database created: " << db_path << " (" << files.size() << " files added)\n";
        return 0;
    }

    if (option == "-script") {
        if (argc < 3) {
            std::cerr << "use: " << argv[0] << " -script <script.py>\n";
            return 1;
        }

        std::string script_path = argv[2];

        Py_Initialize();

        PyRun_SimpleString("import sys; sys.path.append('.')");

        FILE* fp = fopen(script_path.c_str(), "r");
        if (fp) {
            PyRun_SimpleFile(fp, script_path.c_str());
            fclose(fp);
        } else {
            std::cerr << "cant open script file: " << script_path << std::endl;
        }

        Py_Finalize();
        return 0;
    }

    if (option == "-run") {
        if (argc < 3) {
            std::cerr << "put the script to run\n";
            return 1;
        }

        std::string script_path = argv[2];

        g_function_start_times.clear();
        g_hooked_funcs.clear();
        bool hook_mode = false;
        g_time_profiling = false;

        int i = 3;
        while (i < argc) {
            std::string arg = argv[i];
            
            if (arg == "-hook" && i + 1 < argc) {
                hook_mode = true;
                std::stringstream ss(argv[++i]);
                std::string func;
                while (std::getline(ss, func, ',')) {
                    if (!func.empty()) {
                        g_hooked_funcs.insert(func);
                        std::cout << "[HOOK] Added: " << func << std::endl;
                    }
                }
            }
            else if (arg == "-time") {
                g_time_profiling = true;
                std::cout << "[PROFILER] Time profiling enabled" << std::endl;
                i++;
            }
            else {
                std::cerr << "Unknown argument: " << arg << std::endl;
                i++;
            }
        }

        if (!hook_mode) {
            std::cerr << "Error: no hooks" << std::endl;
            return 1;
        }

        if (g_hooked_funcs.empty()) {
            std::cerr << "Error: no hooks" << std::endl;
            return 1;
        }

        Py_Initialize();

        PyObject* globals = PyDict_New();
        PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());

        PyObject* name_obj = PyUnicode_FromString("__main__"); // имя модуля __name__ = '__main__'
        PyDict_SetItemString(globals, "__name__", name_obj);
        Py_DECREF(name_obj);

        PyObject* file_obj = PyUnicode_FromString(script_path.c_str());
        PyDict_SetItemString(globals, "__file__", file_obj); //путь к файлу
        Py_DECREF(file_obj);

        std::string script_dir = fs::path(script_path).parent_path().string(); //для импортов
        std::string add_path = "import sys; sys.path.insert(0, r'" + script_dir + "')";
        PyRun_SimpleString(add_path.c_str());

        if (hook_mode) {
            const char* suppress_py_output = R"PY(
import sys
class _NullWriter:
    def write(self, *_): pass
    def flush(self): pass
sys.stdout = _NullWriter()
sys.stderr = _NullWriter()
)PY";
            PyRun_SimpleString(suppress_py_output);
        }

        PyEval_SetTrace(trace_func, nullptr);

        FILE* fp = fopen(script_path.c_str(), "r");
        if (!fp) {
            std::cerr << "cant open script: " << script_path << "\n";
            PyEval_SetTrace(nullptr, nullptr);
            Py_DECREF(globals);
            Py_Finalize();
            return 1;
        }

        PyObject* result = PyRun_FileExFlags(fp, script_path.c_str(), Py_file_input, globals, globals, 1, nullptr);

        if (!result) PyErr_Print();
        else Py_DECREF(result);

        PyEval_SetTrace(nullptr, nullptr);
        if (g_time_profiling) {
            std::cout << std::endl;
            std::cout << "PROFILING RESULT:" << std::endl;
            
            if (g_time_profiling) {
                std::cout << "TIME PROFILING:" << std::endl;
                if (g_function_start_times.empty()) {
                    std::cout << "   completed successfully" << std::endl;
                } else {

                    
                    std::cout << "   unfinished functions:" << std::endl;
                    for (const auto& entry : g_function_start_times) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - entry.second);
                        double duration_ms = duration.count() / 1000.0;
                        std::cout << "     " << entry.first << ": " << std::fixed << std::setprecision(3) << duration_ms << " ms (unfinished)" << std::endl;
                    }
                }
            }
        }
        Py_DECREF(globals);
        Py_Finalize();

        return 0;
    }


    std::cerr << "Unknown option: " << option << "\n";
    return 1;
}

