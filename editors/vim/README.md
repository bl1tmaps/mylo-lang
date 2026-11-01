# Mylo Vim & Neovim Setup Guide

This guide covers setting up **Syntax Highlighting** and the **Language Server Protocol (LSP)** for Mylo in Vim and Neovim.

---

## 1. Syntax Highlighting

### For Vim Users:
Create the syntax directories:
```bash
mkdir -p ~/.vim/ftdetect ~/.vim/syntax
```
Copy the files:
```bash
cp editors/vim/ftdetect/mylo.vim ~/.vim/ftdetect/
cp editors/vim/syntax/mylo.vim ~/.vim/syntax/
```

### For Neovim Users:
Create the syntax directories:
```bash
mkdir -p ~/.config/nvim/ftdetect ~/.config/nvim/syntax
```
Copy the files:
```bash
cp editors/vim/ftdetect/mylo.vim ~/.config/nvim/ftdetect/
cp editors/vim/syntax/mylo.vim ~/.config/nvim/syntax/
```

---

## 2. Building the Language Server (`mylo-lsp`)

Build `mylo-lsp` from the repository root:
```bash
cmake -B build
cmake --build build --target mylo-lsp
```

Ensure `mylo-lsp` is in your `PATH`:
```bash
sudo cp build/mylo-lsp /usr/local/bin/
```
*(Or specify the direct path to `build/mylo-lsp` in your editor config).*

---

## 3. LSP Setup for Neovim

### Option A: Built-in LSP (Neovim 0.8+) — *Recommended*
Add the following to your `~/.config/nvim/init.lua` (or source `editors/vim/lsp/mylo_nvim.lua`):

```lua
vim.api.nvim_create_autocmd("FileType", {
  pattern = "mylo",
  callback = function(args)
    local lsp_cmd = "mylo-lsp"
    if vim.fn.executable(lsp_cmd) ~= 1 then
      local local_lsp = vim.fn.getcwd() .. "/build/mylo-lsp"
      if vim.fn.executable(local_lsp) == 1 then
        lsp_cmd = local_lsp
      end
    end

    vim.lsp.start({
      name = "mylo-lsp",
      cmd = { lsp_cmd },
      root_dir = vim.fs.dirname(vim.fs.find({ ".git", "CMakeLists.txt" }, { upward = true })[1]) or vim.fn.getcwd(),
      on_attach = function(client, bufnr)
        local opts = { buffer = bufnr }
        vim.keymap.set("n", "K", vim.lsp.buf.hover, opts)
        vim.keymap.set("n", "gd", vim.lsp.buf.definition, opts)
        vim.bo[bufnr].omnifunc = "v:lua.vim.lsp.omnifunc"
      end,
    })
  end,
})
```

### Option B: Using `nvim-lspconfig`
If you use `nvim-lspconfig`, add:
```lua
local lspconfig = require("lspconfig")
local configs = require("lspconfig.configs")

if not configs.mylo_lsp then
  configs.mylo_lsp = {
    default_config = {
      cmd = { "mylo-lsp" },
      filetypes = { "mylo" },
      root_dir = lspconfig.util.root_pattern(".git", "CMakeLists.txt"),
    },
  }
end
lspconfig.mylo_lsp.setup({})
```

---

## 4. LSP Setup for Vim

### Option A: `vim-lsp`
Add to your `~/.vimrc`:
```vim
if executable('mylo-lsp')
  au User lsp_setup call lsp#register_server({
      \ 'name': 'mylo-lsp',
      \ 'cmd': {server_info->['mylo-lsp']},
      \ 'whitelist': ['mylo'],
      \ })
endif
```

### Option B: `coc.nvim`
Run `:CocConfig` and add to `coc-settings.json`:
```json
{
  "languageserver": {
    "mylo": {
      "command": "mylo-lsp",
      "filetypes": ["mylo"],
      "rootPatterns": [".git", "CMakeLists.txt"]
    }
  }
}
```

---

## 5. Features Provided
* **Real-time Diagnostics**: Syntax and type error underlines as you type.
* **Auto-completion**: Built-in standard library functions (with argument signatures), user functions, globals, locals, enums, native C functions (`cfn`), and keywords/types.
* **Hover (`K`)**: Shows type signatures, documentation, and source line numbers.
* **Go-to-Definition (`gd`)**: Jump directly to function declarations.
* **Document Outline / Symbols**: File structure outline and breadcrumbs.
