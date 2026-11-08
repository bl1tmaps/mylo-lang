#pragma once

// Completions, signature help and diagnostics for the editor, from the Mylo compiler itself.

#include <string>
#include <unordered_map>
#include <vector>

enum class LspCompletionKind {
    Function,
    NativeFunction,
    Variable,
    Keyword,
    Type,
    Struct,
    Field,
    EnumMember,
    Module,
    Asset
};

struct LspCompletionItem {
    std::string label;
    std::string insertText;
    std::string signature;
    std::string detail;
    std::string doc;   // The comment written with the declaration, if there is one.
    LspCompletionKind kind = LspCompletionKind::Function;
    int sortPriority = 5;
};

struct LspDiagnostic {
    int line = 1;
    std::string message;
    std::string file;   // Empty when the error is in the document itself.
};

struct LspSignatureHelp {
    bool active = false;
    std::string functionName;
    std::string fullSignature;
    std::vector<std::string> parameters;
    std::string returnType;
    std::string detail;
    int activeParameter = 0;
};

class MyloLspProvider {
public:
    static MyloLspProvider& instance();

    void init();
    // Compiles the document to find its symbols and its first error. documentName is its path,
    // which is used to find the files it imports; it may be empty for an unsaved document.
    void analyzeDocument(const std::string& source, int cursorLine = -1, const std::string& documentName = "");

    std::vector<LspCompletionItem> getCompletions(const std::string& prefix, const std::string& lineUpToCursor) const;
    LspSignatureHelp getSignatureHelp(const std::string& lineUpToCursor) const;
    std::string getHoverInfo(const std::string& word) const;
    const std::vector<LspDiagnostic>& getDiagnostics() const { return m_diagnostics; }

    static const char* getKindBadge(LspCompletionKind kind);

private:
    MyloLspProvider();

    void initBuiltinCatalog();
    void addImportedModules(const std::string& source, const std::string& documentDir);
    void extractSourceSignatures(const std::string& source, std::vector<LspCompletionItem>& outItems,
                                 std::unordered_map<std::string, std::vector<std::string>>& outStructFields,
                                 std::unordered_map<std::string, std::string>& outVarTypes) const;

    struct ImportCache {
        long long stamp = 0;
        std::vector<LspCompletionItem> items;
    };

    bool m_initialized = false;
    std::vector<LspCompletionItem> m_staticItems;      // Standard library, keywords and types.
    std::vector<LspCompletionItem> m_documentItems;    // Symbols from the document and its imports.
    std::vector<LspDiagnostic> m_diagnostics;
    std::unordered_map<std::string, ImportCache> m_importCache;

    std::unordered_map<std::string, std::vector<std::string>> m_structFields;
    std::unordered_map<std::string, std::string> m_varTypes;
};
