; ========================================================================
; LISTING 40
; ========================================================================

bits 16

; Signed displacements
mov ax, [bx + di - 37]
mov [si - 300], cx
mov dx, [bx - 32]

; Explicit sizes
mov [bp + di], byte 7
mov [bx + di], word 468
mov [bp + si + 8], byte 25
mov [bp + di + 30], word 512
mov [di + 901], word 347
mov [si + 1024], byte 30

; Direct address
mov bp, [5]
mov bx, [3458]
mov [60], bp
mov [4852], bx

; Memory-to-accumulator test
mov ax, [2555]
mov ax, [16]
mov al, [56]
mov al, [265]

; Accumulator-to-memory test
mov [2554], ax
mov [15], ax
mov [55], al
mov [264], al