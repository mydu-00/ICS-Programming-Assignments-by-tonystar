/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"
#include "utils.h"
#include <memory/vaddr.h>
#include <stdlib.h>
#include <stdio.h>

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  return -1;
}

static int cmd_help(char *args);

/* new command handlers */
static int cmd_si(char *args) {
  int n = 1;
  if (args) {
    char *endptr = NULL;
    n = strtol(args, &endptr, 10);
    if (endptr == args) n = 1;
  }
  cpu_exec(n);
  return 0;
}

static int cmd_info(char *args) {
  if (!args) {
    printf("Usage: info r|w\n");
    return 0;
  }
  if (strcmp(args, "r") == 0) {
    /* 检查目标架构并打印寄存器 */
#ifdef CONFIG_ISA_riscv32
    /* RISC-V 32位: 打印 x0..x31 和 pc */
    static const char *abi_names[32] = {
      "zero","ra","sp","gp","tp","t0","t1","t2",
      "s0","s1","a0","a1","a2","a3","a4","a5",
      "a6","a7","s2","s3","s4","s5","s6","s7",
      "s8","s9","s10","s11","t3","t4","t5","t6"
    };
    printf("Registers:\n");
    for (int i = 0; i < 32; i++) {
      printf("%-4s x%-2d 0x%08lx\n", abi_names[i], i, (unsigned long)cpu.gpr[i]);
    }
    printf("pc       0x%08lx\n", (unsigned long)cpu.pc);
#elif defined(CONFIG_ISA_riscv64)
    /* RISC-V 64位 */
    static const char *abi_names[32] = {
      "zero","ra","sp","gp","tp","t0","t1","t2",
      "s0","s1","a0","a1","a2","a3","a4","a5",
      "a6","a7","s2","s3","s4","s5","s6","s7",
      "s8","s9","s10","s11","t3","t4","t5","t6"
    };
    printf("Registers:\n");
    for (int i = 0; i < 32; i++) {
      printf("%-4s x%-2d 0x%016lx\n", abi_names[i], i, (unsigned long)cpu.gpr[i]);
    }
    printf("pc       0x%016lx\n", (unsigned long)cpu.pc);
#elif defined(CONFIG_ISA_x86)
    /* x86 */
    printf("Registers:\n");
    printf("eax 0x%08x  ecx 0x%08x  edx 0x%08x  ebx 0x%08x\n",
           reg_l(0), reg_l(1), reg_l(2), reg_l(3));
    printf("esp 0x%08x  ebp 0x%08x  esi 0x%08x  edi 0x%08x\n",
           reg_l(4), reg_l(5), reg_l(6), reg_l(7));
    printf("eip 0x%08x\n", cpu.pc);
#else
    /* Generic fallback */
    printf("Registers (generic):\n");
    for (int i = 0; i < 8; i++) {
      printf("reg%d 0x%08lx\n", i, (unsigned long)cpu.gpr[i]);
    }
    printf("pc   0x%08lx\n", (unsigned long)cpu.pc);
#endif
  } else if (strcmp(args, "w") == 0) {
    info_wp();
  } else {
    printf("Unknown info subcommand '%s'\n", args);
  }
  return 0;
}

static int cmd_x(char *args) {
  if (!args) {
    printf("Usage: x N EXPR\n");
    return 0;
  }
  /* parse N */
  char *tok = strtok(args, " ");
  if (!tok) {
    printf("Usage: x N EXPR\n");
    return 0;
  }
  int N = atoi(tok);
  char *expr_str = tok + strlen(tok) + 1;
  if (expr_str >= args + strlen(args)) expr_str = NULL;
  if (!expr_str) {
    printf("Usage: x N EXPR\n");
    return 0;
  }
  bool ok = false;
  word_t addr = expr(expr_str, &ok);
  if (!ok) {
    printf("Bad expression\n");
    return 0;
  }
  for (int i = 0; i < N; i++) {
    word_t val = vaddr_read(addr + i * 4, 4);
    printf("0x%08lx: 0x%08lx\n", (unsigned long)(addr + i * 4), (unsigned long)val);
  }
  return 0;
}

static int cmd_p(char *args) {
  if (!args) {
    printf("Usage: p EXPR\n");
    return 0;
  }
  bool ok = false;
  word_t val = expr(args, &ok);
  if (!ok) {
    printf("Bad expression\n");
    return 0;
  }
  printf("0x%lx\n", (unsigned long)val);
  return 0;
}

static int cmd_w(char *args) {
  if (!args) {
    printf("Usage: w EXPR\n");
    return 0;
  }
  WP *wp = new_wp(args);
  if (!wp) {
    /* new_wp prints error */
  }
  return 0;
}

static int cmd_d(char *args) {
  if (!args) {
    printf("Usage: d N\n");
    return 0;
  }
  int no = atoi(args);
  if (!delete_wp(no)) {
    /* delete_wp prints error */
  }
  return 0;
}

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Step N instructions (default 1): si [N]", cmd_si },
  { "info", "Print program state: info r|w", cmd_info },
  { "x", "Scan memory: x N EXPR", cmd_x },
  { "p", "Evaluate expression: p EXPR", cmd_p },
  { "w", "Set a watchpoint: w EXPR", cmd_w },
  { "d", "Delete a watchpoint: d N", cmd_d },
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = NULL;
  if (args) arg = strtok(args, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
