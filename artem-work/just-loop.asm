.data

msg:
    .ascii      "Hello, ARM!\n"
len = . - msg

.text

.globl _start
_start:
    /* initialise loop variables */
    mov     %r3, $4    /* r3 = 10 */
loop:
    /* loop code, synthetic example */
    addlt   %r1, %r1, %r2
    sub     %r2, %r2, %r1
    subgt   %r1, %r1, %r2
    add     %r2, %r2, %r1

    /* check loop condition */
    sub     %r3, %r3, $1 /* r3 -- */
    cmp     %r3, $0     /* compare r3 with 0 */
    bne     loop        /* branch to loop if r3 != 0 */

    /* syscall exit(int status) */
    mov     %r0, $0     /* status -> 0 */
    mov     %r7, $1     /* exit is syscall #1 */
    swi     $0          /* invoke syscall */
