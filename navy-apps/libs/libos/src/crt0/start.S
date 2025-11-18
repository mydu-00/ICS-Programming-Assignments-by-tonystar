.globl  _start
_start:

#if defined(__ISA_AM_NATIVE__)

  movq $0, %rbp
  andq $0xfffffffffffffff0, %rsp
  movq %rax, %rdi
  call call_main

#elif defined(__ISA_X86_64__)

  movq $0, %rbp
  movq %rax, %rsp
  movq %rax, %rdi
  call call_main

#elif defined(__ISA_X86__)

  movl $0, %ebp
  call call_main

#elif defined(__ISA_MIPS32__)

  move $fp, $zero
  jal call_main

#elif defined(__riscv)
  // Nanos-lite 将用户栈顶放在 a0(GPRx)，这里用它来设置 sp
  mv   sp, a0
  mv   s0, zero
  jal  call_main

#elif defined(__ISA_LOONGARCH32R__)

  move $fp, $zero
  bl call_main

#else
#error Unsupported ISA
#endif
