if exists("b:current_syntax")
  finish
endif

syn keyword nInclude import from
syn keyword nDecl type union fun method intf delegate
syn keyword nStorageClass inline extern
syn keyword nExport export

syn keyword nDecl pre _pre post _post invariant _invariant example _example assert alias
syn keyword nDecl contract honors _honors pretag posttag tag

syn keyword nConditional if elif else match
syn keyword nRepeat while continue break
syn keyword nRepeat for pfor foreach pforeach
syn keyword nStatement let return block future lambda
syn keyword nOperator in and or not
syn keyword nOperator sizeof
syn keyword nKeyword pass as attr declare
syn keyword nException try catch throw except
syn match nOperator "[@:]"

syn keyword nKeyword self this

syn keyword Constant null false true

syn region nComment start="--" skip="\\$" end="$" keepend contains=@Spell,nTodo
syn keyword nTodo contained TODO FIXME XXX

syn region nSemantic start="^\s*#[!~?]" end="$" keepend
syn match nMutate "[!][^=]"me=e-1
syn match nMercurial "#"
syn match nNullable "?"
syn match nWildcard "\$"

syn match nIntf "i_\w\+"

syn match nSpaceError display excludenl "\s\+$"
syn match nSpaceError display "^\ *\t"me=e-1

"integer number, or floating point number without a dot and with "f".
syn case ignore
syn match	nNumbers	display transparent "\<\d\|\.\d" contains=nNumber,nFloat,nOctalError,nOctal
" Same, but without octal error (for comments)
syn match	nNumbersCom	display contained transparent "\<\d\|\.\d" contains=nNumber,nFloat,nOctal
syn match	nNumber		display contained "\d\+\(u\=l\{0,2}\|ll\=u\)\>"
"hex number
syn match	nNumber		display contained "0x\x\+\(u\=l\{0,2}\|ll\=u\)\>"
" Flag the first zero of an octal number as something special
syn match	nOctal		display contained "0\o\+\(u\=l\{0,2}\|ll\=u\)\>" contains=nOctalZero
syn match	nOctalZero	display contained "\<0"
syn match	nFloat		display contained "\d\+f"
"floating point number, with dot, optional exponent
syn match	nFloat		display contained "\d\+\.\d*\(e[-+]\=\d\+\)\=[fl]\="
"floating point number, starting with a dot, optional exponent
syn match	nFloat		display contained "\.\d\+\(e[-+]\=\d\+\)\=[fl]\=\>"
"floating point number, without dot, with exponent
syn match	nFloat		display contained "\d\+e[-+]\=\d\+[fl]\=\>"
"hexadecimal floating point number, optional leading digits, with dot, with exponent
syn match	nFloat		display contained "0x\x*\.\x\+p[-+]\=\d\+[fl]\=\>"
"hexadecimal floating point number, with leading digits, optional dot, with exponent
syn match	nFloat		display contained "0x\x\+\.\=p[-+]\=\d\+[fl]\=\>"

" flag an octal number with wrong digits
syn match	nOctalError	display contained "0\o*[89]\d*"
syn case match

syn match	nSpecial	display contained "\\\(x\x\+\|\o\{1,3}\|.\|$\)"
syn match	nSpecial	display contained "\\\(u\x\{4}\|U\x\{8}\)"
syn match	nFormat		display "%\(\d\+\$\)\=[-+' #0*]*\(\d*\|\*\|\*\d\+\$\)\(\.\(\d*\|\*\|\*\d\+\$\)\)\=\([hlLjzt]\|ll\|hh\)\=\([aAbdiuoxXDOUfFeEgGcCsSpn]\|\[\^\=.[^]]*\]\)" contained
syn match	nFormat		display "%%" contained
syn region	nString		start=+L\="+ skip=+\\\\\|\\"+ end=+"+ contains=cSpecial,cFormat,@Spell
syn region	nString		start=+L\='+ skip=+\\\\\|\\'+ end=+'+ contains=cSpecial,cFormat,@Spell

hi def link nDecl Structure
hi def link nInclude Include
hi def link nStorageClass StorageClass
hi def link nConditional Conditional
hi def link nRepeat Repeat
hi def link nStatement Statement
hi def link nOperator Operator
hi def link nException Exception
hi def link nKeyword Keyword
hi def link nComment Comment
hi def link nTodo Todo
hi def link nSpaceError Error
hi def link nNumber Number
hi def link nFloat Number
hi def link nOctal Number
hi def link nNumbers Number
hi def link nString String
hi def link nSpecial SpecialChar
hi def link nSemantic Semantic
hi def link nMutate Constant
hi def link nMercurial Constant
hi def link nNullable Constant
hi def link nWildcard Special
hi def link nIntf Type
hi def link nFunction Function
hi def link nExport Special

let b:current_syntax = "n"
