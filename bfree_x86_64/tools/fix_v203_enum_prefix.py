from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()
names = ["length", "prototype", "constructor", "name", "callee", "lastIndex",
         "index", "input", "toString", "toLocaleString", "valueOf"]
for name in names:
    t = t.replace(f"String_{name}", f"ExecutionEngine::String_{name}")
t = t.replace("ExecutionEngine::ExecutionEngine::", "ExecutionEngine::")
p.write_text(t)
print("[fix] ExecutionEngine:: prefix on JSStrings slots")
