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
/* forward declaration of expr and init from sdb/expr.c */
extern void init_regex(void); /* ensure regex compiled for expr */
extern word_t expr(char *e, bool *success);

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();

int main(int argc, char *argv[]) {
  /* If first argument is a file, run expr test harness mode */
  if (argc > 1) {
    FILE *f = fopen(argv[1], "r");
    if (f) {
      /* initialize minimal parts needed by expr() */
      init_regex();

      char line[65536];
      unsigned total = 0, passed = 0, failed = 0;
      while (fgets(line, sizeof(line), f)) {
        /* skip empty lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n') continue;

        /* line format: "<unsigned> <expression...>\n" */
        unsigned expect = 0;
        char *expr_str = NULL;

        /* find first space separating number and expression */
        char *sp = strchr(p, ' ');
        if (!sp) continue;
        *sp = '\0';
        expect = (unsigned)strtoul(p, NULL, 10);
        expr_str = sp + 1;
        /* trim trailing newline */
        char *nl = strchr(expr_str, '\n');
        if (nl) *nl = '\0';

        /* evaluate using NEMU expr() */
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
    /* else no file: fallthrough to normal init */
  }

  /* Initialize the monitor. */
#ifdef CONFIG_TARGET_AM
  am_init_monitor();
#else
  init_monitor(argc, argv);
#endif

  /* Start engine. */
  engine_start();

  return is_exit_status_bad();
}
