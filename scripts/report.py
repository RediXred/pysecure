import analyzer

db = analyzer.open("src.myund")

print("Files in project:")
for f in db.files():
    print(f.path)

db.create_call_graph("cg.dot")
db.create_graph("inh.dot")

db.ents("ms_manager.py", True)

imports = db.get_all_imports("used_libraries.txt")

for imp in imports:
    print(f"File ID: {imp.file_id}, Module: {imp.module}, Name: {imp.name}")


for f in db.get_dangerous():
    print(f.function, f.line, f.file)