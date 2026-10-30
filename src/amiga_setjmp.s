    .text
    .even
    .globl _setjmp
    .globl setjmp
    .type _setjmp, @function
    .type setjmp, @function
_setjmp:
setjmp:
    movel 4(%sp), %a0
    movel (%sp), (%a0)
    moveml %d1-%d7, 4(%a0)
    moveml %a0-%a7, 32(%a0)
    moveq #0, %d0
    rts

    .even
    .globl _longjmp
    .globl longjmp
    .type _longjmp, @function
    .type longjmp, @function
_longjmp:
longjmp:
    movel 4(%sp), %a0
    movel 8(%sp), %d0
    bne 1f
    moveq #1, %d0
1:
    moveml 4(%a0), %d1-%d7
    moveml 36(%a0), %a1-%a7
    movel (%a0), (%sp)
    movel 32(%a0), %a0
    rts
