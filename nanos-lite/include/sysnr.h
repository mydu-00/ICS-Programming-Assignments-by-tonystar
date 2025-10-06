#ifndef SYSNR_H
#define SYSNR_H

// 与 Linux 一致的号 (示例，只列出你当前用到的)
// 1 = SYS_write 在 Linux, 这里你只用到 yield/exit, 自行约定即可
#define SYS_yield 1
#define SYS_exit  60

#endif