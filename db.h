#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <unordered_set>
#include <memory>


struct File {
    int id;
    std::string path;
};

struct Function {
    int id;
    std::string name;
    int class_id;
    int file_id;
    int start_line;
    int end_line;
};

struct Reference {
    int id;
    int from_id;
    int to_id;
    std::string kind;
};

struct Class {
    int id;
    std::string name;
    int file_id;
    int start_line;
    int end_line;
};

struct ClassNode {
    int id;
    std::string name;
};

struct ClassEdge {
    int from;
    int to;
};

struct FuncInfo {
    std::string name;
    std::string class_name;
};

struct Import {
    int file_id;
    std::string module;
    std::string name;
};

struct DangerousCall {
    std::string function;
    std::string from;
    int line;
    std::string file;
};

class DB {
private:
    sqlite3* conn;

public:

    DB(const std::string& path);
    ~DB();

    void add_file(const std::string& path);
    void add_class(const std::string& name, int file_id, int start_line, int end_line);
    void add_function(const std::string& name, int file_id, int class_id, int start_line, int end_line, const std::string& args);
    void add_reference(int from_id, int to_id, const std::string& kind, const std::string& args = "");

    std::vector<File> files();
    void create_graph(const std::string& output_file="inheritance.dot");
    void create_call_graph(const std::string& output_file="call_graph.dot");
    void ents(const std::string& filename, bool include_builtin);
    std::vector<Import> get_all_imports(const std::string& save_file);
    std::vector<DangerousCall> get_dangerous();
    

    int last_insert_id();

    int get_class_id_by_name(const std::string& class_name);
    void add_import(int file_id, const std::string& module, const std::string& name);
    int get_function_id_by_name(const std::string& func_name);
    int get_function_id_by_name_class(const std::string& func_name, int class_id);
    
    bool is_project_module(const std::string& module) const;
};
