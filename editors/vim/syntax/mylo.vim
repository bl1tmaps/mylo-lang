if exists("b:current_syntax")
  finish
endif

" 1. Keywords and Types (Based on TK_ enum in compiler.c)
syntax keyword myloKeyword fn cfn var if else elif for in ret print mod import native break continue enum module_path true false forever embed region clear monitor debugger
syntax keyword myloType any num str f64 f32 f16 i64 i32 i16 i8 u64 u32 u16 u8 byte bytes bool

" 2. Comments - Defined as // until newline in compiler.c
syntax region myloComment start="//" end="$" contains=@NoGroup

" 3. Special Characters and Symbols
syntax match myloNamespace "::"
syntax match myloBraces "[{}]"
syntax match myloRange "\.\.\."
syntax match myloArrow "->"

" 4. Strings (Standard, Format, and Byte strings)
syntax region myloString start='"' end='"' skip='\\"'
syntax region myloString start='f"' end='"' skip='\\"'
syntax region myloString start='b"' end='"' skip='\\"'

" 5. Numbers
syntax match myloNumber "\v<\d+>"
syntax match myloNumber "\v<\d+\.\d+>"

" 6. Highlight Links
highlight default link myloKeyword Keyword
highlight default link myloType Type
highlight default link myloComment Comment
highlight default link myloNamespace Special
highlight default link myloBraces Delimiter
highlight default link myloRange Boolean
highlight default link myloArrow Operator
highlight default link myloString String
highlight default link myloNumber Number

let b:current_syntax = "mylo"
