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

#include <common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* expr() 和 init_regex() 在 sdb/expr.c 中 */
extern void init_regex(void);
extern word_t expr(char *e, bool *success);

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();

int main(int argc, char *argv[]) {
  /* 测试模式：如果第一个参数是 .txt 文件，则把它当作表达式测试的 input */
  const char *ext = argc > 1 ? strrchr(argv[1], '.') : NULL;
  if (argc > 1 && ext && strcmp(ext, ".txt") == 0) {
    FILE *f = fopen(argv[1], "r");
    if (f) {
      init_regex(); /* 确保正则编译，expr() 依赖它 */
      char line[65536];
      unsigned total = 0, passed = 0, failed = 0;
      while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n') continue;
        /* 格式: "<unsigned> <expression...>" */
        char *sp = strchr(p, ' ');
        if (!sp) continue;
        *sp = '\0';
        unsigned expect = (unsigned)strtoul(p, NULL, 10);
        char *expr_str = sp + 1;
        char *nl = strchr(expr_str, '\n'); if (nl) *nl = '\0';
        bool ok = false;
        word_t v = expr(expr_str, &ok);
        total++;
        if (!ok) {
          failed++;
          fprintf(stderr, "ERR eval failed: %s\n", expr_str);
        } else if ((unsigned)v != expect) {
          failed++;
          fprintf(stderr, "Mismatch: expect=%u got=%u  expr=\"%s\"\n", expect, (unsigned)v, expr_str);
        } else {
          passed++;
        }
      }
      fclose(f);
      printf("TEST RESULT: total=%u passed=%u failed=%u\n", total, passed, failed);
      return failed ? 1 : 0;
    }
    /* 若不是可读文件，继续正常启动 */
  }

  /* 原有 normal init/运行流程 */
#ifdef CONFIG_TARGET_AM
  am_init_monitor();
#else
  init_monitor(argc, argv);
#endif

  engine_start();
  return is_exit_status_bad();
}
