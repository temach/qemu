hello: .asciz "Hello world.\n\r"

.global main

main:         
    adr r1, hello
    b test
loop:
    ldr r12, =0x16000000
    str r0, [r12]
test:   
    ldrb r0, [r1], #1
    cmp r0,#0
    bne loop
