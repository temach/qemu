.data

msg:
    .ascii      "Hello, ARM!\n"
len = . - msg

.balign 4           /* align on next 4 bytes boundary */
buffer: .word 0         /* declare 4 byte int and init to 0 */

.text

.globl _start
_start:
    /* initialise loop variables */
    mov     %r3, $4     /* r3 = 10 */
    mov     %r1, $0     /* r1 = 0 */
    mov     %r2, $0     /* r2 = 0 */

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

check_1:
    /* setup */
    mov     %r1, $0     /* r1 = 0 */
    mov     %r2, $0     /* r2 = 0 */
    /* test */
    sub     %r2, %r2, %r1
    add     %r2, %r2, %r1
    /* print */
    mov     %r4, %r2        /* move the value to print to r4 */
    add     %r4, %r4, 0x30  /* add zero ascii char */
    mov     [buffer], %r4   /* move ascii char to buffer */
    /* syscall write(int fd, const void *buf, size_t count) */
    mov     %r0, $1     /* fd -> stdout */
    ldr     %r1, addr_of_buffer /* r1 = &buffer */
    ldr     %r1, =buffer   /* buf -> msg */
    ldr     %r2, $4     /* count -> len(msg), only print 4 bytes */
    mov     %r7, $4     /* write is syscall #4 */
    swi     $0          /* invoke syscall */

    /* syscall write(int fd, const void *buf, size_t count) */
    mov     %r0, $1     /* fd -> stdout */
    ldr     %r1, =msg   /* buf -> msg */
    ldr     %r2, =len   /* count -> len(msg) */
    mov     %r7, $4     /* write is syscall #4 */
    swi     $0          /* invoke syscall */

    /* syscall exit(int status) */
    mov     %r0, $0     /* status -> 0 */
    mov     %r7, $1     /* exit is syscall #1 */
    swi     $0          /* invoke syscall */

/* label to access the data "addr_of_buffer = &buffer" */
addr_of_buffer: .word buffer
