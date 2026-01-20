#include "db.h"
#include <iostream>
#include <sqlite3.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>



DB::DB(const std::string& path) {
    if (sqlite3_open(path.c_str(), &conn)) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(conn) << std::endl;
        conn = nullptr;
    }
}

DB::~DB() {
    if (conn) sqlite3_close(conn);
}

void DB::add_file(const std::string& path) {
    if (!conn) return;
    sqlite3_stmt* stmt;
    std::string sql = "INSERT INTO files(path) VALUES(?);";
    sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DB::add_class(const std::string& name, int file_id, int start_line, int end_line) {
    if (!conn) return;
    sqlite3_stmt* stmt;
    std::string sql = "INSERT INTO classes(name,file_id,start_line,end_line) VALUES(?,?,?,?);";
    sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, file_id);
    sqlite3_bind_int(stmt, 3, start_line);
    sqlite3_bind_int(stmt, 4, end_line);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DB::add_function(
    const std::string& name,
    int file_id,
    int class_id,
    int start_line,
    int end_line,
    const std::string& args
) {
    sqlite3_stmt* stmt;
    const char* sql =
        "INSERT INTO functions(name, file_id, class_id, start_line, end_line, args) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, file_id);
    sqlite3_bind_int(stmt, 3, class_id);
    sqlite3_bind_int(stmt, 4, start_line);
    sqlite3_bind_int(stmt, 5, end_line);
    sqlite3_bind_text(stmt, 6, args.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}


void DB::add_reference(int from_id, int to_id,
                       const std::string& kind,
                       const std::string& args)
{
    sqlite3_stmt* stmt;
    const char* sql =
        "INSERT INTO refs(from_id, to_id, kind, args) VALUES (?, ?, ?, ?)";

    sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, from_id);
    sqlite3_bind_int(stmt, 2, to_id);
    sqlite3_bind_text(stmt, 3, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, args.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}



std::vector<File> DB::files() {
    std::vector<File> result;
    if (!conn) return result;

    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, path FROM files;";
    if (sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            File f;
            f.id = sqlite3_column_int(stmt, 0);
            f.path = (const char*)sqlite3_column_text(stmt, 1);
            result.push_back(f);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

int DB::last_insert_id() {
    return (conn) ? (int)sqlite3_last_insert_rowid(conn) : 0;
}

int DB::get_class_id_by_name(const std::string& class_name) {
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id FROM classes WHERE name=? LIMIT 1;";
    sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, class_name.c_str(), -1, SQLITE_STATIC);

    int id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DB::get_function_id_by_name(const std::string& func_name) {
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id FROM functions WHERE name=? LIMIT 1;";
    sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, func_name.c_str(), -1, SQLITE_STATIC);

    int id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

int DB::get_function_id_by_name_class(const std::string& func_name, int class_id) {
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id FROM functions WHERE name=? AND class_id=? LIMIT 1;";
    sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, func_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, class_id);

    int id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

void DB::create_graph(const std::string& output_file) {
    std::vector<ClassNode> classes;
    std::vector<ClassEdge> edges;

    const char* sql_classes = "SELECT id, name FROM classes;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(conn, sql_classes, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "fail to prepare statement: " << sqlite3_errmsg(conn) << "\n";
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char* name_text = sqlite3_column_text(stmt, 1);
        std::string name = name_text ? reinterpret_cast<const char*>(name_text) : "";
        classes.push_back({id, name});
    }
    sqlite3_finalize(stmt);

    //наследования
    const char* sql_refs = "SELECT from_id, to_id FROM refs WHERE kind='inherit';";
    if (sqlite3_prepare_v2(conn, sql_refs, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "fail to prepare statement: " << sqlite3_errmsg(conn) << "\n";
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int from = sqlite3_column_int(stmt, 0);
        int to = sqlite3_column_int(stmt, 1);
        edges.push_back({from, to});
    }
    sqlite3_finalize(stmt);

    //.dot
    std::ofstream ofs(output_file);
    if (!ofs.is_open()) {
        std::cerr << "fail to open file: " << output_file << "\n";
        return;
    }

    ofs << "digraph InheritanceGraph {\n";
    ofs << "  node [shape=box, style=filled, color=lightblue];\n";

    for (auto& c : classes) {
        ofs << "  " << c.id << " [label=\"" << c.name << "\"];\n";
    }

    for (auto& e : edges) {
        ofs << "  " << e.from << " -> " << e.to << ";\n";
    }

    ofs << "}\n";
    ofs.close();

    std::cout << ".dot created: " << output_file << "\n";
}

void DB::create_call_graph(const std::string& output_file) {
    struct Node { int id; std::string name; int class_id; bool builtin; };
    struct Edge { int from; int to; std::string args; bool builtin; };

    std::vector<Node> nodes;
    std::vector<Edge> edges;
    sqlite3_stmt* stmt = nullptr;

    std::map<int,std::string> class_names;
    if (sqlite3_prepare_v2(conn, "SELECT id, name FROM classes;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int cid = sqlite3_column_int(stmt, 0);
            const unsigned char* t = sqlite3_column_text(stmt, 1);
            class_names[cid] = t ? reinterpret_cast<const char*>(t) : ("class" + std::to_string(cid));
        }
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(conn, "SELECT id, name, class_id FROM functions;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const unsigned char* t = sqlite3_column_text(stmt, 1);
            int class_id = sqlite3_column_int(stmt, 2);
            std::string name = t ? reinterpret_cast<const char*>(t) : ("fn" + std::to_string(id));
            nodes.push_back({id, name, class_id, false});
        }
    }
    sqlite3_finalize(stmt);

    //call refs
    if (sqlite3_prepare_v2(conn, "SELECT from_id, to_id, kind, args FROM refs WHERE kind LIKE 'call%';", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int from = sqlite3_column_int(stmt, 0);
            int to = sqlite3_column_int(stmt, 1);
            const unsigned char* kindt = sqlite3_column_text(stmt, 2);
            const unsigned char* argst = sqlite3_column_text(stmt, 3);
            std::string kind = kindt ? reinterpret_cast<const char*>(kindt) : "";
            std::string args = argst ? reinterpret_cast<const char*>(argst) : "";
            bool builtin = (kind.rfind("call_builtin:", 0) == 0);
            edges.push_back({from, to, args, builtin});
        }
    }
    sqlite3_finalize(stmt);

    //group by class
    std::map<int, std::vector<int>> by_class;
    for (auto &n : nodes) by_class[n.class_id].push_back(n.id);
    auto escape_dot = [](const std::string &s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    };
    auto truncate = [](const std::string &s, size_t max_len=48) {
        if (s.size() <= max_len) return s;
        return s.substr(0, max_len-3) + std::string("...");
    };



    std::string dot_file = output_file;
    std::ofstream ofs(dot_file);
    if (!ofs.is_open()) {
        fprintf(stderr, "cant open %s\n", dot_file.c_str());
        return;
    }

    ofs << "digraph G {\n";
    ofs << "  graph [splines=false, overlap=scale, concentrate=true, nodesep=1.2, ranksep=1.2];\n";
    ofs << "  node [shape=box, style=filled, fontname=\"Helvetica\", fontsize=10];\n";
    ofs << "  edge [fontname=\"Helvetica\", fontsize=8, arrowhead=vee, arrowsize=0.7];\n";

    //кластеры
    for (auto &kv : by_class) {
        int class_id = kv.first;
        auto &list = kv.second;
        if (class_id == 0) { //глобальные
            ofs << "  subgraph cluster_globals {\n";
            ofs << "    label=\"global functions\"; style=dashed; color=gray90;\n";
            for (int fid : list) {
                auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Node& n){ return n.id==fid; });
                if (it!=nodes.end())
                    ofs << "    f" << it->id << " [label=\"" << escape_dot(truncate(it->name)) << "\", fillcolor=lightgoldenrodyellow];\n";
            }
            ofs << "  }\n";
        } else {
            std::string cname = class_names.count(class_id) ? class_names[class_id] : ("class" + std::to_string(class_id));
            ofs << "  subgraph cluster_class_" << class_id << " {\n";
            ofs << "    label=\"" << escape_dot(cname) << "\"; style=rounded; color=lightgrey;\n";
            for (int fid : list) {
                auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Node& n){ return n.id==fid; });
                if (it!=nodes.end())
                    ofs << "    f" << it->id << " [label=\"" << escape_dot(truncate(it->name)) << "\", fillcolor=lightgreen];\n";
            }
            ofs << "  }\n";
        }
    }



    //edges
    std::set<std::pair<int,int>> seen_edges;
    for (auto &e : edges) {
        if (e.from == 0 || e.to == 0) continue;
        std::pair<int,int> key{e.from,e.to};
        if (seen_edges.count(key)) continue;
        seen_edges.insert(key);



        std::string style = e.builtin ? "style=dashed, color=gray" : "style=solid, color=black";
        std::string lab = truncate(e.args, 36);
        if (!lab.empty())
            ofs << "  f" << e.from << " -> f" << e.to << " [xlabel=\"" << escape_dot(lab) << "\", " << style << "];\n";
        else
            ofs << "  f" << e.from << " -> f" << e.to << " [" << style << "];\n";
    }

    ofs << "}\n";
    ofs.close();
}

void DB::ents(const std::string& filename, bool include_builtin) {
    

    sqlite3_stmt* stmt;

    std::map<int, FuncInfo> all_funcs;

    const char* sql_all_funcs =
        "SELECT f.id, f.name, c.name "
        "FROM functions f "
        "LEFT JOIN classes c ON f.class_id = c.id;";

    if (sqlite3_prepare_v2(conn, sql_all_funcs, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int fid = sqlite3_column_int(stmt, 0);

            const char* fname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* cname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            all_funcs[fid] = {
                fname ? fname : "<unnamed>",
                cname ? cname : ""
            };
        }
    }
    sqlite3_finalize(stmt);



    int file_id = 0;
    std::string full_path;

    const char* sql_file = "SELECT id, path FROM files WHERE path LIKE '%' || ?;";

    if (sqlite3_prepare_v2(conn, sql_file, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            file_id = sqlite3_column_int(stmt, 0);
            full_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
    }
    sqlite3_finalize(stmt);

    if (!file_id) {
        fprintf(stderr, "File not found: %s\n", filename.c_str());
        return;
    }

    std::ofstream ofs(filename + ".ents.txt");
    ofs << "FILE: " << full_path << "\n";
    ofs << "===\n\n";


    ofs << "[CLASSES]\n";

    const char* sql_classes =
        "SELECT name, start_line, end_line "
        "FROM classes WHERE file_id = ?;";

    if (sqlite3_prepare_v2(conn, sql_classes, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, file_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ofs << "Name: " << reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) << "\n";
            ofs << "Defined at: lines " << sqlite3_column_int(stmt, 1) << "-" << sqlite3_column_int(stmt, 2) << "\n\n";
        }
    }
    sqlite3_finalize(stmt);
    ofs << "[FUNCTIONS]\n";

    const char* sql_funcs =
        "SELECT f.id, f.name, c.name, f.start_line, f.end_line, f.args "
        "FROM functions f "
        "LEFT JOIN classes c ON f.class_id = c.id "
        "WHERE f.file_id = ?;";

    if (sqlite3_prepare_v2(conn, sql_funcs, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, file_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* fname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* cname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* args = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));


            if (cname)
                ofs << "Name: " << cname << "." << fname << "\n";
            else
                ofs << "Name: " << fname << "\n";

            ofs << "Type: function\n";
            ofs << "Defined at: lines " << sqlite3_column_int(stmt, 3) << "-" << sqlite3_column_int(stmt, 4) << "\n";
            ofs << "Args: " << (args ? args : "") << "\n\n";
        }
    }
    sqlite3_finalize(stmt);

    ofs << "[CALLS]\n";

    const char* sql_calls =
        "SELECT r.from_id, r.to_id, r.kind, r.args "
        "FROM refs r "
        "JOIN functions f ON r.from_id = f.id "
        "WHERE f.file_id = ?;";

    if (sqlite3_prepare_v2(conn, sql_calls, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, file_id);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int from_id = sqlite3_column_int(stmt, 0);
            int to_id = sqlite3_column_int(stmt, 1);

            std::string kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            std::string args = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            //from
            auto& from = all_funcs[from_id];
            if (!from.class_name.empty())
                ofs << "From: " << from.class_name << "." << from.name << "\n";
            else
                ofs << "From: " << from.name << "\n";

            //to
            if (to_id != 0) {
                auto it = all_funcs.find(to_id);
                if (it != all_funcs.end()) {
                    if (!it->second.class_name.empty())
                        ofs << "To: " << it->second.class_name << "." << it->second.name << "\n";
                    else
                        ofs << "To: " << it->second.name << "\n";
                } else {
                    ofs << "To: <function #" << to_id << ">\n";
                }
                ofs << "Type: call\n";
            } else {
                if (!include_builtin)
                    continue;

                std::string target = kind;
                if (kind.rfind("call_builtin:", 0) == 0)
                    target = kind.substr(strlen("call_builtin:"));

                ofs << "To: " << target << "\n";
                ofs << "Type: builtin\n";
            }

            ofs << "Args: " << args << "\n\n";
        }
    }
    sqlite3_finalize(stmt);

    ofs.close();
}

void DB::add_import(int file_id, const std::string& module, const std::string& name) {
    const char* sql = "INSERT INTO imports(file_id, module, name) VALUES(?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, file_id);
        sqlite3_bind_text(stmt, 2, module.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

bool DB::is_project_module(const std::string& module) const {
    //модуль считается проектным если есть файл с таким именем в БД
    sqlite3_stmt* stmt;
    const char* sql = "SELECT 1 FROM files WHERE path LIKE ? LIMIT 1;";
    bool result = false;
    if (sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + module + ".py";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) result = true;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<Import> DB::get_all_imports(const std::string& save_file = "") {
    std::vector<Import> result;
    std::set<std::string> top_modules;

    const char* sql = "SELECT file_id, module, name FROM imports;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Import imp;
            imp.file_id = sqlite3_column_int(stmt, 0);
            const unsigned char* module_text = sqlite3_column_text(stmt, 1);
            const unsigned char* name_text = sqlite3_column_text(stmt, 2);
            imp.module = module_text ? reinterpret_cast<const char*>(module_text) : "";
            imp.name = name_text   ? reinterpret_cast<const char*>(name_text)   : "";
            result.push_back(imp);

            // топ-модуль
            if (!imp.module.empty()) {
                size_t pos = imp.module.find('.');
                std::string top = (pos != std::string::npos) ? imp.module.substr(0, pos) : imp.module;
                top_modules.insert(top);
            }
        }
    }
    sqlite3_finalize(stmt);

    if (!save_file.empty()) {
        std::ofstream out(save_file);
        for (const auto& m : top_modules) out << m << "\n";
        out.close();
    }

    return result;
}

std::vector<DangerousCall> DB::get_dangerous() {
    std::vector<DangerousCall> result;
    if (!conn) return result;

    std::unordered_set<std::string> dangerous = {
        "eval", "exec", "execfile", "compile",
        "os.system", "os.popen", "os.popen2", "os.popen3", "os.popen4",
        "os.execv", "os.execve", "os.execvp", "os.execl", "os.execle",
        "os.execlp", "os.execvpe", "os.spawnv", "os.spawnve",
        "subprocess.call", "subprocess.Popen", "subprocess.run",
        "subprocess.check_call", "subprocess.check_output",
        "open", "file", "__builtins__.open",
        "socket.socket", "socket.create_connection", "socket.create_server",
        "pickle.load", "pickle.loads",
        "marshal.load", "marshal.loads",
        "yaml.load", "yaml.load_all",
        "__import__",
        "input", "getpass.getpass", "builtins.input",
        "sqlite3.connect.execute", "sqlite3.connect.executemany",
        "sqlite3.Cursor.execute", "sqlite3.Cursor.executemany"
    };

    sqlite3_stmt* stmt;
    //возвращает все вызовы функций с информацией о функции источнике и файле
    const char* sql = 
        "SELECT r.id, r.from_id, r.to_id, r.kind, r.args, "
        "f.name as from_name, c.name as from_class, "
        "f.file_id, f.start_line, fl.path as file_path "
        "FROM refs r "
        "JOIN functions f ON r.from_id = f.id "
        "LEFT JOIN classes c ON f.class_id = c.id "
        "JOIN files fl ON f.file_id = fl.id "
        "WHERE r.kind LIKE 'call_%' "
        "ORDER BY fl.path, f.start_line;";

    if (sqlite3_prepare_v2(conn, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int ref_id = sqlite3_column_int(stmt, 0);
            int from_id = sqlite3_column_int(stmt, 1);

            int to_id = sqlite3_column_int(stmt, 2);


            const char* kind_c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            const char* args_c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            const char* from_name_c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            const char* from_class_c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));


            int file_id = sqlite3_column_int(stmt, 7);
            int line = sqlite3_column_int(stmt, 8);
            const char* file_path_c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));

            std::string kind = kind_c ? kind_c : "";
            std::string args = args_c ? args_c : "";
            std::string from_name = from_name_c ? from_name_c : "";
            std::string from_class = from_class_c ? from_class_c : "";
            std::string file_path = file_path_c ? file_path_c : "";
            
            std::string from_func;
            if (!from_name.empty()) {
                if (!from_class.empty()) {
                    from_func = from_class + "." + from_name;
                } else {
                    from_func = from_name;
                }
            } else {
                from_func = "<anonymous>";
            }

            //определение вызываемой функции
            std::string target_func;
            bool is_external_call = false;
            
            if (kind.rfind("call_builtin:", 0) == 0) {
                target_func = kind.substr(13);
                is_external_call = true;
            } 
            if (is_external_call) {
                size_t pipe_pos = target_func.find('|');
                if (pipe_pos != std::string::npos) {
                    target_func = target_func.substr(0, pipe_pos);
                }
                if (!target_func.empty()) {
                    if (target_func.front() == '"' || target_func.front() == '\'') {
                        target_func = target_func.substr(1);
                    }
                    if (target_func.back() == '"' || target_func.back() == '\'') {
                        target_func.pop_back();
                    }
                    size_t paren_pos = target_func.find('(');
                    if (paren_pos != std::string::npos) {
                        target_func = target_func.substr(0, paren_pos);
                    }
                }
            }
            else if (to_id != 0) {
                sqlite3_stmt* func_stmt;
                const char* func_sql = "SELECT name, class_id FROM functions WHERE id = ?;";
                if (sqlite3_prepare_v2(conn, func_sql, -1, &func_stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(func_stmt, 1, to_id);
                    if (sqlite3_step(func_stmt) == SQLITE_ROW) {
                        const char* target_name_c = reinterpret_cast<const char*>(sqlite3_column_text(func_stmt, 0));
                        int target_class_id = sqlite3_column_int(func_stmt, 1);
                        
                        std::string target_name = target_name_c ? target_name_c : "";
                        if (target_class_id != 0) {
                            sqlite3_stmt* class_stmt;
                            const char* class_sql = "SELECT name FROM classes WHERE id = ?;";
                            if (sqlite3_prepare_v2(conn, class_sql, -1, &class_stmt, nullptr) == SQLITE_OK) {
                                sqlite3_bind_int(class_stmt, 1, target_class_id);
                                if (sqlite3_step(class_stmt) == SQLITE_ROW) {
                                    const char* class_name_c = reinterpret_cast<const char*>(sqlite3_column_text(class_stmt, 0));
                                    std::string class_name = class_name_c ? class_name_c : "";
                                    target_func = class_name + "." + target_name;
                                }
                                sqlite3_finalize(class_stmt);
                            }
                        } else {
                            target_func = target_name;
                        }
                    }
                    sqlite3_finalize(func_stmt);
                }
            }

            if (!target_func.empty()) {
                bool is_dangerous = false;
                
                if (dangerous.count(target_func)) {
                    is_dangerous = true;
                }
                //проверка модуля (например os.*)
                else {
                    size_t dot_pos = target_func.find('.');
                    if (dot_pos != std::string::npos) {
                        std::string module = target_func.substr(0, dot_pos);
                        std::string func_name = target_func.substr(dot_pos + 1);
                        
                        for (const auto& danger_func : dangerous) {
                            if (danger_func.find(module + ".") == 0) {
                                is_dangerous = true;
                                break;
                            }
                        }
                    }
                    else if (dangerous.count(target_func)) {
                        is_dangerous = true;
                    }
                }

                if (is_dangerous) {
                    DangerousCall dc;
                    dc.function = target_func;
                    dc.from = from_func;
                    dc.line = line;
                    dc.file = file_path;
                    result.push_back(dc);
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    return result;
}
