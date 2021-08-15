; haribote-os boot asm
; TAB=4

BOTPAK  EQU     0x00280000      ; 引导bootpack
DSKCAC  EQU     0x00100000      ; 磁盘缓存位置
DSKCAC0 EQU     0x00008000      ; 磁盘缓存位置（真实模式）

; 有关BOOT_INFO
CYLS    EQU     0x0ff0          ; 设定启动区
LEDS    EQU     0x0ff1
VMODE   EQU     0x0ff2          ; 关于颜色数目的信息。颜色的位数
SCRNX   EQU     0x0ff4          ; 分辨率的X（screen x） 
SCRNY   EQU     0x0ff6          ; 分辨率的Y（screen y）
VRAM    EQU     0x0ff8          ; 图像缓冲区的开始地址

        ORG     0xc200          ; 指明程序的装载地址

; 设置屏幕模式

        MOV     AL, 0x13        ; VGA显卡，320x200x8位彩色
        MOV     AH, 0x00
        INT     0x10
        MOV     BYTE [VMODE], 8 ; 记录画面模式
        MOV     WORD [SCRNX], 320
        MOV     WORD [SCRNY], 200
        MOV     DWORD [VRAM], 0x000a0000

; 用BIOS取得键盘上各种LED指示灯的状态

        MOV     AH, 0x02
        INT     0x16            ; keyboard BIOS
        MOV     [LEDS], AL

; 防止 PIC 接受任何中断
; 在 AT 兼容机器的规格中，如果 PIC 初始化，
; 则必须在 CLI 之前这样做，否则偶尔会挂起。
; 稍后我们将初始化 PIC。

        MOV     AL, 0xff
        OUT     0x21, AL
        NOP                     ; 因为当OUT指令连续运行时会有一个模块不能正常工作
        OUT     0xa1, AL

        CLI                     ; 此外，在CPU级别也禁止中断

; 配置 A20GATE，以便 CPU 可以访问超过 1MB 的内存

        CALL    waitkbdout
        MOV     AL, 0xd1
        OUT     0x64, AL
        CALL    waitkbdout
        MOV     AL, 0xdf        ; 使能A20
        OUT     0x60, AL
        CALL    waitkbdout

; 保护模式转换

[INSTRSET "i486p"]              ; 这个程序是给486用的

        LGDT    [GDTR0]         ; 设置临时GDT
        MOV     EAX, CR0
        AND     EAX, 0x7fffffff ; 将位 31 设置为 0（禁止分页）
        OR      EAX, 0x00000001 ; 将位 0 设置为 1（用于保护模式转换）
        MOV     CR0, EAX
        JMP     pipelineflush
pipelineflush:
        MOV     AX, 1*8         ; 读写段 32 位
        MOV     DS, AX
        MOV     ES, AX
        MOV     FS, AX
        MOV     GS, AX
        MOV     SS, AX

; bootpack传输

        MOV     ESI, bootpack   ; 传送源
        MOV     EDI, BOTPAK     ; 传送目的
        MOV     ECX, 512*1024/4
        CALL    memcpy

; 接着，将盘数据传送到原来位置

; 首先，从引导扇区开始

        MOV     ESI, 0x7c00     ; 传送源
        MOV     EDI, DSKCAC     ; 传送目的
        MOV     ECX, 512/4
        CALL    memcpy

; 其余全部

        MOV     ESI, DSKCAC0+512 ; 传送源
        MOV     EDI, DSKCAC+512  ; 传送目的
        MOV     ECX, 0
        MOV     CL, BYTE [CYLS]
        IMUL    ECX, 512*18*2/4  ; 从气缸数转换为字节数/4
        SUB     ECX, 512/4       ; 扣除 IPL 的分钟
        CALL    memcpy

; 我已经完成了所有我必须做的
; 剩下的就交给机器人包了

; 启动bootpack

        MOV     EBX, BOTPAK
        MOV     ECX, [EBX + 16]
        ADD     ECX, 3          ; ECX += 3
        SHR     ECX, 2          ; ECX /= 4
        JZ      skip            ; 没有什么可转发的
        MOV     ESI, [EBX + 20] ; 传送源
        ADD     ESI, EBX
        MOV     EDI, [EBX + 12] ; 传送目的
        CALL    memcpy
skip:
        MOV     ESP, [EBX + 12] ; 堆栈初始值
        JMP     DWORD 2*8:0x0000001b

waitkbdout:
        IN      AL, 0x64
        AND     AL, 0x02
        JNZ     waitkbdout      ; 如果 AND 结果不为 0，则到等待输出
        RET 

memcpy:
        MOV     EAX, [ESI]
        ADD     ESI, 4
        MOV     [EDI], EAX
        ADD     EDI, 4
        SUB     ECX, 1
        JNZ     memcpy          ; 如果减去的结果不是 0，则到 memcpy
        RET 
; memcpy 可以写入字符串指令，除非您忘记输入地址大小前缀。

        ALIGNB  16
GDT0:
        RESB    8               ; 空选择器
        DW      0xffff, 0x0000, 0x9200, 0x00cf ; 读写段 32 位
        DW      0xffff, 0x0000, 0x9a28, 0x0047 ; 可执行段32bit（用于bootpack）

        DW      0
GDTR0:
        DW      8*3-1
        DD      GDT0

        ALIGNB  16
bootpack:
