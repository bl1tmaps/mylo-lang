#include "mylo_lsp.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <setjmp.h>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

extern "C" {
#include "defines.h"
#include "vm.h"
#include "compiler.h"
#include "mylolib.h"
}

static std::string s_lastErrorMsg;
static int s_lastErrorLine = -1;
static bool s_hasError = false;

static void lspErrorCallback(const char* msg) {
    s_hasError = true;
    s_lastErrorLine = -1;
    s_lastErrorMsg.clear();
    if (!msg) return;

    const char* p = std::strstr(msg, "[Line ");
    if (p) {
        p += 6;
        s_lastErrorLine = std::atoi(p);
        const char* errStart = std::strstr(p, "] Error: ");
        if (errStart) {
            errStart += 9;
            s_lastErrorMsg = errStart;
        } else {
            s_lastErrorMsg = msg;
        }
    } else {
        s_lastErrorMsg = msg;
    }
}

static void demangleSymbol(const char* mangled, char* outLabel, bool isEnum) {
    std::strcpy(outLabel, mangled);
    char* sep = std::strrchr(outLabel, '_');
    if (sep && (isEnum || (outLabel[0] >= 'A' && outLabel[0] <= 'Z'))) {
        std::memmove(sep + 2, sep + 1, std::strlen(sep + 1) + 1);
        sep[0] = ':';
        sep[1] = ':';
    }
}

// Splits a line at its "//" comment, ignoring any inside a string or character literal.
static void splitComment(const std::string& line, std::string& outCode, std::string& outComment) {
    char quote = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            outCode = line.substr(0, i);
            outComment = line.substr(i + 2);
            return;
        }
    }
    outCode = line;
    outComment.clear();
}

static std::string trimmed(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

static void countBraces(const std::string& code, int& outOpens, int& outCloses) {
    outOpens = 0;
    outCloses = 0;
    char quote = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '{') {
            ++outOpens;
        } else if (c == '}') {
            ++outCloses;
        }
    }
}

MyloLspProvider& MyloLspProvider::instance() {
    static MyloLspProvider s_instance;
    return s_instance;
}

MyloLspProvider::MyloLspProvider() {
    init();
}

const char* MyloLspProvider::getKindBadge(LspCompletionKind kind) {
    switch (kind) {
        case LspCompletionKind::Function:       return "fn";
        case LspCompletionKind::NativeFunction: return "cfn";
        case LspCompletionKind::Variable:       return "var";
        case LspCompletionKind::Keyword:        return "kw";
        case LspCompletionKind::Type:           return "type";
        case LspCompletionKind::Struct:         return "struct";
        case LspCompletionKind::Field:          return "field";
        case LspCompletionKind::EnumMember:     return "enum";
        case LspCompletionKind::Module:         return "mod";
        case LspCompletionKind::Asset:          return "asset";
        default:                                return "item";
    }
}

void MyloLspProvider::init() {
    if (m_initialized) return;
    m_initialized = true;

    initBuiltinCatalog();
}

void MyloLspProvider::initBuiltinCatalog() {
    m_staticItems.clear();

    // 1. Mylo Standard Library
    for (int i = 0; std_library[i].name != nullptr; ++i) {
        const auto& def = std_library[i];
        std::string sig = "fn " + std::string(def.name) + "(";
        for (int a = 0; a < def.arg_count; ++a) {
            if (a > 0) sig += ", ";
            sig += "arg" + std::to_string(a + 1) + ": ";
            sig += (def.arg_types[a] ? def.arg_types[a] : "any");
        }
        sig += ") -> " + std::string(def.ret_type ? def.ret_type : "void");

        LspCompletionItem item;
        item.label = def.name;
        item.insertText = def.name;
        item.signature = sig;
        item.detail = "Mylo Standard Library";
        item.kind = LspCompletionKind::Function;
        item.sortPriority = 1;
        m_staticItems.push_back(item);
    }

    // 2. Keywords
    const char* const keywords[] = {
        "fn", "cfn", "if", "else", "elif", "for", "in", "forever",
        "break", "continue", "ret", "return", "print", "struct", "mod",
        "import", "native", "enum", "true", "false", "embed", "self",
        "monitor", "debugger", "region", "clear", "const", "let", "var", nullptr
    };
    for (int i = 0; keywords[i] != nullptr; ++i) {
        LspCompletionItem item;
        item.label = keywords[i];
        item.insertText = keywords[i];
        item.signature = "keyword " + std::string(keywords[i]);
        item.detail = "Language Keyword";
        item.kind = LspCompletionKind::Keyword;
        item.sortPriority = 6;
        m_staticItems.push_back(item);
    }

    // 3. Strict Types
    const char* const types[] = {
        "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64",
        "f16", "f32", "f64", "bool", "str", "string", "bytes", "any", "void", nullptr
    };
    for (int i = 0; types[i] != nullptr; ++i) {
        LspCompletionItem item;
        item.label = types[i];
        item.insertText = types[i];
        item.signature = "type " + std::string(types[i]);
        item.detail = "Primitive Type";
        item.kind = LspCompletionKind::Type;
        item.sortPriority = 5;
        m_staticItems.push_back(item);
    }
}

namespace {

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The quoted argument of a line like `import "x.mylo"` or `module_path("dir")`, if it has one.
bool quotedArgument(const std::string& line, const char* keyword, std::string& out) {
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line.compare(start, std::strlen(keyword), keyword) != 0) return false;
    size_t open = line.find('"', start);
    if (open == std::string::npos) return false;
    size_t close = line.find('"', open + 1);
    if (close == std::string::npos) return false;
    out = line.substr(open + 1, close - open - 1);
    return true;
}

fs::path findInTree(const fs::path& root, const std::string& name, int depth) {
    std::error_code ec;
    if (depth < 0 || !fs::is_directory(root, ec)) return {};
    fs::path direct = root / name;
    if (fs::is_regular_file(direct, ec)) return direct;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec)) {
            fs::path found = findInTree(entry.path(), name, depth - 1);
            if (!found.empty()) return found;
        }
    }
    return {};
}

} // namespace

// Adds the symbols of every file the document imports, found the way the compiler finds them.
void MyloLspProvider::addImportedModules(const std::string& source, const std::string& documentDir) {
    std::vector<fs::path> searchDirs;
    if (!documentDir.empty()) searchDirs.emplace_back(documentDir);
    for (int i = 0; i < MyloVMOpts.module_path_count; ++i) searchDirs.emplace_back(MyloVMOpts.module_paths[i]);

    std::vector<std::string> imports;
    std::istringstream stream(source);
    std::string line, arg;
    while (std::getline(stream, line)) {
        if (quotedArgument(line, "module_path", arg)) {
            fs::path dir(arg);
            if (dir.is_relative() && !documentDir.empty()) dir = fs::path(documentDir) / dir;
            searchDirs.push_back(dir);
        } else if (quotedArgument(line, "import", arg)) {
            imports.push_back(arg);
        }
    }

    for (const auto& name : imports) {
        fs::path found;
        std::error_code ec;
        for (const auto& dir : searchDirs) {
            if (fs::is_regular_file(dir / name, ec)) { found = dir / name; break; }
        }
        if (found.empty()) found = findInTree(fs::path(mylo_exe_dir) / "modules", name, 3);
        if (found.empty()) found = findInTree(fs::path(mylo_exe_dir) / ".." / "modules", name, 3);
        if (found.empty()) continue;

        std::string key = fs::absolute(found, ec).lexically_normal().string();
        long long stamp = (long long)fs::last_write_time(found, ec).time_since_epoch().count();
        ImportCache& cache = m_importCache[key];
        if (cache.items.empty() || cache.stamp != stamp) {
            cache.items.clear();
            cache.stamp = stamp;
            std::unordered_map<std::string, std::vector<std::string>> fields;
            std::unordered_map<std::string, std::string> types;
            extractSourceSignatures(readText(found), cache.items, fields, types);
            for (auto& item : cache.items) {
                if (item.detail.find("Variable") == std::string::npos) item.detail += " (" + found.filename().string() + ")";
            }
        }
        m_documentItems.insert(m_documentItems.end(), cache.items.begin(), cache.items.end());
    }
}

void MyloLspProvider::extractSourceSignatures(
    const std::string& source,
    std::vector<LspCompletionItem>& outItems,
    std::unordered_map<std::string, std::vector<std::string>>& outStructFields,
    std::unordered_map<std::string, std::string>& outVarTypes) const
{
    std::istringstream stream(source);
    std::string rawLine;
    std::string currentMod = "";
    std::string currentStruct = "";

    // Each block ends when the nesting falls back to the depth it was opened at.
    int depth = 0;
    int modDepth = -1;
    int structDepth = -1;
    int fnDepth = -1;
    int nativeDepth = -1;

    std::string pendingDoc;   // Comment lines directly above the current line.

    while (std::getline(stream, rawLine)) {
        std::string code, comment;
        splitComment(rawLine, code, comment);
        std::string line = trimmed(code);
        std::string commentText = trimmed(comment);

        if (line.empty()) {
            // A blank line separates a comment from whatever follows it.
            if (commentText.empty()) pendingDoc.clear();
            else if (nativeDepth < 0) pendingDoc += (pendingDoc.empty() ? "" : "\n") + commentText;
            continue;
        }

        int opens = 0, closes = 0;
        countBraces(line, opens, closes);
        int depthBefore = depth;
        depth = std::max(0, depth + opens - closes);
        bool opensBlock = opens > closes;

        // A comment above a declaration documents it; one beside it is the fallback.
        std::string doc = pendingDoc.empty() ? commentText : pendingDoc;
        pendingDoc.clear();

        auto leaveFinishedBlocks = [&]() {
            if (closes == 0) return;
            if (nativeDepth >= 0 && depth <= nativeDepth) nativeDepth = -1;
            if (fnDepth >= 0 && depth <= fnDepth) fnDepth = -1;
            if (structDepth >= 0 && depth <= structDepth) { structDepth = -1; currentStruct = ""; }
            if (modDepth >= 0 && depth <= modDepth) { modDepth = -1; currentMod = ""; }
        };

        // C blocks hold C code, not Mylo declarations.
        if (nativeDepth >= 0) {
            leaveFinishedBlocks();
            continue;
        }
        bool startsNative = line.rfind("C(", 0) == 0 || line.rfind("C {", 0) == 0 || line.rfind("C{", 0) == 0;
        if (opensBlock && (startsNative || line.find("= C(") != std::string::npos)) nativeDepth = depthBefore;
        if (startsNative) {
            leaveFinishedBlocks();
            continue;
        }

        if (line.rfind("mod ", 0) == 0) {
            std::string modName = line.substr(4);
            auto brace = modName.find('{');
            if (brace != std::string::npos) modName = modName.substr(0, brace);
            while (!modName.empty() && (modName.back() == ' ' || modName.back() == '\t')) modName.pop_back();
            currentMod = modName;
            currentStruct = "";
            if (opensBlock) modDepth = depthBefore;

            LspCompletionItem it;
            it.label = modName;
            it.insertText = modName;
            it.signature = "mod " + modName;
            it.detail = "Module Namespace";
            it.doc = doc;
            it.kind = LspCompletionKind::Module;
            it.sortPriority = 4;
            outItems.push_back(it);
            continue;
        }

        if (line.rfind("struct ", 0) == 0) {
            std::string sName = line.substr(7);
            auto brace = sName.find('{');
            if (brace != std::string::npos) sName = sName.substr(0, brace);
            while (!sName.empty() && (sName.back() == ' ' || sName.back() == '\t')) sName.pop_back();
            if (opensBlock) {
                currentStruct = sName;
                structDepth = depthBefore;
            }

            std::string fullStruct = currentMod.empty() ? sName : (currentMod + "::" + sName);
            LspCompletionItem it;
            it.label = fullStruct;
            it.insertText = fullStruct;
            it.signature = "struct " + fullStruct;
            it.detail = "Data Structure Definition";
            it.doc = doc;
            it.kind = LspCompletionKind::Struct;
            it.sortPriority = 4;
            outItems.push_back(it);
            continue;
        }

        if (!currentStruct.empty() && line.find(':') != std::string::npos && line.find("fn ") == std::string::npos) {
            auto colon = line.find(':');
            std::string fieldName = line.substr(0, colon);
            while (!fieldName.empty() && (fieldName.back() == ' ' || fieldName.back() == '\t')) fieldName.pop_back();
            std::string fieldType = line.substr(colon + 1);
            while (!fieldType.empty() && (fieldType.front() == ' ' || fieldType.front() == '\t')) fieldType.erase(fieldType.begin());
            auto scPos = fieldType.find_first_of(" \t\r\n,}");
            if (scPos != std::string::npos) fieldType = fieldType.substr(0, scPos);

            std::string fullStruct = currentMod.empty() ? currentStruct : (currentMod + "::" + currentStruct);
            outStructFields[fullStruct].push_back(fieldName);

            LspCompletionItem it;
            it.label = fieldName;
            it.insertText = fieldName;
            it.signature = fieldName + ": " + fieldType;
            it.detail = "Field of " + fullStruct;
            it.doc = doc;
            it.kind = LspCompletionKind::Field;
            it.sortPriority = 4;
            outItems.push_back(it);
            leaveFinishedBlocks();
            continue;
        }

        // Functions and CFN
        bool isFn = (line.rfind("fn ", 0) == 0);
        bool isCfn = (line.rfind("cfn ", 0) == 0);
        if (isFn || isCfn) {
            std::string header = line;
            auto brace = header.find('{');
            if (brace != std::string::npos) header = header.substr(0, brace);
            while (!header.empty() && (header.back() == ' ' || header.back() == '\t')) header.pop_back();

            size_t p0 = header.find('(');
            if (p0 != std::string::npos) {
                size_t startName = isCfn ? 4 : 3;
                std::string fnName = header.substr(startName, p0 - startName);
                while (!fnName.empty() && (fnName.back() == ' ' || fnName.back() == '\t')) fnName.pop_back();
                while (!fnName.empty() && (fnName.front() == ' ' || fnName.front() == '\t')) fnName.erase(fnName.begin());

                std::string fullFn = currentMod.empty() ? fnName : (currentMod + "::" + fnName);
                std::string fullSig = isCfn ? ("cfn " + fullFn + header.substr(p0)) : ("fn " + fullFn + header.substr(p0));

                LspCompletionItem it;
                it.label = fullFn;
                it.insertText = fullFn;
                it.signature = fullSig;
                it.detail = isCfn ? "Native C Function" : "User Function";
                it.doc = doc;
                it.kind = isCfn ? LspCompletionKind::NativeFunction : LspCompletionKind::Function;
                it.sortPriority = 2;
                outItems.push_back(it);
                if (opensBlock) fnDepth = depthBefore;
                continue;
            }
        }

        // Variable declarations: name: Type = val
        if (line.find(':') != std::string::npos && line.find('=') != std::string::npos) {
            auto colon = line.find(':');
            std::string varName = line.substr(0, colon);
            while (!varName.empty() && (varName.back() == ' ' || varName.back() == '\t')) varName.pop_back();
            while (!varName.empty() && (varName.front() == ' ' || varName.front() == '\t')) varName.erase(varName.begin());

            if (varName.rfind("const ", 0) == 0) varName = varName.substr(6);
            if (varName.rfind("let ", 0) == 0) varName = varName.substr(4);
            if (varName.rfind("var ", 0) == 0) varName = varName.substr(4);

            if (!varName.empty() && (std::isalnum((unsigned char)varName[0]) || varName[0] == '_')) {
                bool isLocal = fnDepth >= 0;
                std::string fullVar = (currentMod.empty() || isLocal) ? varName : (currentMod + "::" + varName);
                std::string rest = line.substr(colon + 1);
                auto eq = rest.find('=');
                std::string vType = (eq != std::string::npos) ? rest.substr(0, eq) : rest;
                while (!vType.empty() && (vType.back() == ' ' || vType.back() == '\t')) vType.pop_back();
                while (!vType.empty() && (vType.front() == ' ' || vType.front() == '\t')) vType.erase(vType.begin());

                outVarTypes[fullVar] = vType;

                LspCompletionItem it;
                it.label = fullVar;
                it.insertText = fullVar;
                it.signature = fullVar + ": " + vType;
                it.detail = (currentMod.empty() || isLocal) ? "Variable" : ("Constant of " + currentMod);
                it.doc = doc;
                it.kind = LspCompletionKind::Variable;
                it.sortPriority = 3;
                outItems.push_back(it);
            }
        }

        leaveFinishedBlocks();
    }
}

void MyloLspProvider::analyzeDocument(const std::string& source, int cursorLine, const std::string& documentName) {
    (void)cursorLine;
    m_documentItems.clear();
    m_diagnostics.clear();
    m_structFields.clear();
    m_varTypes.clear();

    if (source.empty()) return;

    // 1. Textual extraction for quick signatures & struct/module hierarchy, including what the
    // document imports.
    std::string documentDir;
    if (!documentName.empty()) {
        std::error_code ec;
        documentDir = fs::absolute(documentName, ec).parent_path().string();
    }
    extractSourceSignatures(source, m_documentItems, m_structFields, m_varTypes);
    addImportedModules(source, documentDir);

    // 2. Run compiler parser in debug mode to extract exact symbols & diagnostics
    VM vm;
    std::memset(&vm, 0, sizeof(VM));
    vm_init(&vm);
    compiler_reset();

    const MyloConfigType savedConfig = MyloConfig;
    s_hasError = false;
    s_lastErrorLine = -1;
    s_lastErrorMsg.clear();

    MyloConfig.debug_mode = true;
    MyloConfig.error_callback = lspErrorCallback;

    jmp_buf lspEnv;
    MyloConfig.repl_jmp_buf = &lspEnv;
    // Check the code without loading native libraries into the IDE.
    MyloConfig.build_mode = true;

    // Imports are found relative to the document, as when it runs.
    char savedDir[sizeof(current_file_dir)];
    std::memcpy(savedDir, current_file_dir, sizeof(savedDir));
    if (!documentDir.empty()) std::snprintf(current_file_dir, sizeof(current_file_dir), "%s", documentDir.c_str());

    std::string errorFile;
    char* copy = (char*)std::malloc(source.size() + 1);
    if (copy) {
        std::memcpy(copy, source.c_str(), source.size() + 1);
        compiler_set_main_file(&vm, documentName.empty() ? "untitled.mylo" : documentName.c_str());
        if (setjmp(lspEnv) == 0) {
            parse(&vm, copy);
        } else {
            errorFile = compiler_current_file();
        }
        std::free(copy);
    }
    MyloConfig = savedConfig;
    std::memcpy(current_file_dir, savedDir, sizeof(savedDir));

    // Diagnostics. An error inside an imported file is reported on the import's file.
    if (s_hasError && s_lastErrorLine > 0) {
        LspDiagnostic diag;
        diag.line = s_lastErrorLine;
        diag.message = s_lastErrorMsg.empty() ? "Syntax error" : s_lastErrorMsg;
        std::error_code ec;
        if (!errorFile.empty() && !documentName.empty() &&
            !fs::equivalent(fs::path(errorFile), fs::path(documentName), ec)) {
            diag.file = errorFile;
        }
        m_diagnostics.push_back(diag);
    }

    // The compiler only knows function names, so anything already catalogued with its full
    // signature must keep it. Symbols are compared in the compiler's mangled form.
    std::unordered_set<std::string> catalogued;
    auto addCatalogued = [&](const std::vector<LspCompletionItem>& items) {
        for (const auto& it : items) {
            if (it.kind != LspCompletionKind::Function && it.kind != LspCompletionKind::NativeFunction) continue;
            std::string mangled = it.label;
            auto sep = mangled.find("::");
            if (sep != std::string::npos) {
                catalogued.insert(mangled.substr(sep + 2));   // cfns are listed by their bare C name.
                mangled.replace(sep, 2, "_");
            }
            catalogued.insert(mangled);
        }
    };
    addCatalogued(m_documentItems);
    addCatalogued(m_staticItems);

    // Live functions
    for (int i = 0; i < func_count; ++i) {
        char displayName[256];
        demangleSymbol(funcs[i].name, displayName, false);
        std::string nameStr = displayName;

        if (catalogued.find(funcs[i].name) == catalogued.end()) {
            LspCompletionItem item;
            item.label = nameStr;
            item.insertText = nameStr;
            item.signature = "fn " + nameStr + "(...)";
            item.detail = "User Function";
            item.kind = LspCompletionKind::Function;
            item.sortPriority = 2;
            m_documentItems.push_back(item);
        }
    }

    // Live CFN
    for (int i = 0; i < ffi_count; ++i) {
        if (ffi_blocks[i].is_cfn && ffi_blocks[i].c_symbol_name[0] &&
            catalogued.find(ffi_blocks[i].c_symbol_name) == catalogued.end()) {
            std::string nameStr = ffi_blocks[i].c_symbol_name;
            std::string retType = ffi_blocks[i].return_type[0] ? ffi_blocks[i].return_type : "void";
            LspCompletionItem item;
            item.label = nameStr;
            item.insertText = nameStr;
            item.signature = "cfn " + nameStr + "(...) -> " + retType;
            item.detail = "Native C Function";
            item.kind = LspCompletionKind::NativeFunction;
            item.sortPriority = 2;
            m_documentItems.push_back(item);
        }
    }

    // Live Globals
    for (int i = 0; i < global_count; ++i) {
        char displayName[256];
        demangleSymbol(globals[i].name, displayName, false);
        std::string nameStr = displayName;
        LspCompletionItem item;
        item.label = nameStr;
        item.insertText = nameStr;
        item.signature = "global " + nameStr;
        item.detail = "Global Variable";
        item.kind = LspCompletionKind::Variable;
        item.sortPriority = 3;
        m_documentItems.push_back(item);
    }

    // Live Locals
    for (int i = 0; i < local_count; ++i) {
        std::string nameStr = locals[i].name;
        LspCompletionItem item;
        item.label = nameStr;
        item.insertText = nameStr;
        item.signature = "local " + nameStr;
        item.detail = "Local Variable";
        item.kind = LspCompletionKind::Variable;
        item.sortPriority = 3;
        m_documentItems.push_back(item);
    }

    // Live Enums
    for (int i = 0; i < enum_entry_count; ++i) {
        char displayName[256];
        demangleSymbol(enum_entries[i].name, displayName, true);
        std::string nameStr = displayName;
        LspCompletionItem item;
        item.label = nameStr;
        item.insertText = nameStr;
        item.signature = "enum " + nameStr;
        item.detail = "Enum Entry";
        item.kind = LspCompletionKind::EnumMember;
        item.sortPriority = 4;
        m_documentItems.push_back(item);
    }

    vm_cleanup(&vm);
}

static bool ciContains(const std::string& str, const std::string& sub) {
    if (sub.empty()) return true;
    if (sub.size() > str.size()) return false;
    auto it = std::search(str.begin(), str.end(), sub.begin(), sub.end(),
                          [](char ch1, char ch2) { return std::tolower((unsigned char)ch1) == std::tolower((unsigned char)ch2); });
    return (it != str.end());
}

static bool ciStartsWith(const std::string& str, const std::string& sub) {
    if (sub.size() > str.size()) return false;
    for (size_t i = 0; i < sub.size(); ++i) {
        if (std::tolower((unsigned char)str[i]) != std::tolower((unsigned char)sub[i])) return false;
    }
    return true;
}

std::vector<LspCompletionItem> MyloLspProvider::getCompletions(const std::string& prefix, const std::string& lineUpToCursor) const {
    std::vector<LspCompletionItem> results;
    std::unordered_map<std::string, bool> seen;

    // Check if user is scoping into a namespace: Module:: or Enum::
    auto scopePos = lineUpToCursor.rfind("::");
    std::string modPrefix = "";
    if (scopePos != std::string::npos) {
        size_t idStart = scopePos;
        while (idStart > 0 && (std::isalnum((unsigned char)lineUpToCursor[idStart - 1]) || lineUpToCursor[idStart - 1] == '_')) {
            idStart--;
        }
        modPrefix = lineUpToCursor.substr(idStart, scopePos - idStart);
    }

    auto consider = [&](const LspCompletionItem& item) {
        if (seen.find(item.label) != seen.end()) return;

        bool match = false;
        if (!modPrefix.empty()) {
            std::string expectedStart = modPrefix + "::";
            if (ciStartsWith(item.label, expectedStart)) {
                std::string member = item.label.substr(expectedStart.size());
                if (prefix.empty() || ciContains(member, prefix)) {
                    match = true;
                }
            }
        } else {
            if (prefix.empty()) {
                match = true;
            } else if (ciStartsWith(item.label, prefix)) {
                match = true;
            } else if (ciContains(item.label, prefix)) {
                match = true;
            }
        }

        if (match) {
            seen[item.label] = true;
            results.push_back(item);
        }
    };

    // Priority 1: Document items (the script and its imports)
    for (const auto& it : m_documentItems) consider(it);
    // Priority 2: Static items (standard library, keywords, types)
    for (const auto& it : m_staticItems) consider(it);

    // Sort by priority, then by exact prefix match, then alphabetical
    std::sort(results.begin(), results.end(), [&](const LspCompletionItem& a, const LspCompletionItem& b) {
        if (a.sortPriority != b.sortPriority) return a.sortPriority < b.sortPriority;
        bool aStart = ciStartsWith(a.label, prefix);
        bool bStart = ciStartsWith(b.label, prefix);
        if (aStart != bStart) return aStart > bStart;
        return a.label < b.label;
    });

    if (results.size() > 40) {
        results.resize(40);
    }
    return results;
}

LspSignatureHelp MyloLspProvider::getSignatureHelp(const std::string& lineUpToCursor) const {
    LspSignatureHelp help;
    help.active = false;

    // Scan backwards from cursor to find open '(' without matching ')'
    int parenDepth = 0;
    int commaCount = 0;
    int openParenIdx = -1;

    for (int i = (int)lineUpToCursor.size() - 1; i >= 0; --i) {
        char c = lineUpToCursor[i];
        if (c == ')') {
            parenDepth++;
        } else if (c == '(') {
            if (parenDepth == 0) {
                openParenIdx = i;
                break;
            } else {
                parenDepth--;
            }
        } else if (c == ',' && parenDepth == 0) {
            commaCount++;
        }
    }

    if (openParenIdx <= 0) return help;

    // Extract function identifier before '('
    int endId = openParenIdx - 1;
    while (endId >= 0 && (lineUpToCursor[endId] == ' ' || lineUpToCursor[endId] == '\t')) endId--;
    if (endId < 0) return help;

    int startId = endId;
    while (startId >= 0 && (std::isalnum((unsigned char)lineUpToCursor[startId]) ||
                           lineUpToCursor[startId] == '_' ||
                           lineUpToCursor[startId] == ':')) {
        startId--;
    }
    startId++;
    if (startId > endId) return help;

    std::string fnName = lineUpToCursor.substr(startId, endId - startId + 1);

    // Look up function signature
    auto findItem = [&](const std::vector<LspCompletionItem>& list) -> const LspCompletionItem* {
        for (const auto& it : list) {
            if (it.label == fnName && (it.kind == LspCompletionKind::Function || it.kind == LspCompletionKind::NativeFunction)) {
                return &it;
            }
        }
        return nullptr;
    };

    const LspCompletionItem* found = findItem(m_documentItems);
    if (!found) found = findItem(m_staticItems);
    if (!found) return help;

    help.active = true;
    help.functionName = found->label;
    help.fullSignature = found->signature;
    help.detail = found->detail;
    help.activeParameter = commaCount;

    // Parse parameters from signature
    auto p1 = found->signature.find('(');
    auto p2 = found->signature.rfind(')');
    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
        std::string paramList = found->signature.substr(p1 + 1, p2 - p1 - 1);
        std::istringstream pStream(paramList);
        std::string pTok;
        while (std::getline(pStream, pTok, ',')) {
            while (!pTok.empty() && (pTok.front() == ' ' || pTok.front() == '\t')) pTok.erase(pTok.begin());
            while (!pTok.empty() && (pTok.back() == ' ' || pTok.back() == '\t')) pTok.pop_back();
            if (!pTok.empty()) help.parameters.push_back(pTok);
        }
    }

    auto arrow = found->signature.rfind("->");
    if (arrow != std::string::npos) {
        std::string ret = found->signature.substr(arrow + 2);
        while (!ret.empty() && (ret.front() == ' ' || ret.front() == '\t')) ret.erase(ret.begin());
        help.returnType = ret;
    }

    return help;
}

std::string MyloLspProvider::getHoverInfo(const std::string& word) const {
    if (word.empty()) return "";

    auto checkList = [&](const std::vector<LspCompletionItem>& list) -> std::string {
        for (const auto& it : list) {
            if (it.label == word) {
                std::string res = it.signature;
                if (!it.detail.empty()) res += "\n" + it.detail;
                return res;
            }
        }
        return "";
    };

    std::string s = checkList(m_documentItems);
    if (!s.empty()) return s;
    s = checkList(m_staticItems);
    if (!s.empty()) return s;

    return "";
}
