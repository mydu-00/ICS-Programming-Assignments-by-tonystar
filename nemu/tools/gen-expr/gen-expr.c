/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
* (trimmed header)
***************************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

static char buf[65536] = {};
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

/* helper: append string to buf with overflow check */
static bool append_str(const char *s) {
  size_t need = strlen(s);
  if (strlen(buf) + need + 1 >= sizeof(buf)) return false;
  strcat(buf, s);
  return true;
}

/* helper: append random spaces (0..3) */
static void append_spaces() {
  int n = rand() % 4;
  for (int i = 0; i < n; i++) {
    if (!append_str(" ")) return;
  }
}

/* generate a random unsigned number literal as decimal or hex */
static bool gen_number_literal(bool nonzero) {
  unsigned v;
  /* choose range to avoid extremely large decimal strings when printing */
  if (rand() % 4 == 0) {
    /* hex */
    do { v = (unsigned)rand(); } while (nonzero && v == 0);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "0x%08x", v);
    if (!append_str(tmp)) return false;
    return true;
  } else {
    /* decimal */
    do { v = (unsigned)rand(); } while (nonzero && v == 0);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%u", v);
    if (!append_str(tmp)) return false;
    return true;
  }
}

/* recursively generate expression. depth controls size, nonzero_right indicates we must produce non-zero value. */
static bool gen_expr(int depth, bool nonzero_required) {
  if (depth <= 0) {
    /* leaf: produce a number literal (honor nonzero requirement) */
    return gen_number_literal(nonzero_required);
  }

  int choice = rand() % 5;
  if (choice == 0) {
    /* parenthesized expression */
    if (!append_str("(")) return false;
    append_spaces();
    if (!gen_expr(depth - 1, nonzero_required)) return false;
    append_spaces();
    if (!append_str(")")) return false;
    return true;
  } else if (choice == 1) {
    /* unary minus */
    if (!append_str("-")) return false;
    append_spaces();
    return gen_expr(depth - 1, nonzero_required);
  } else {
    /* binary operator */
    /* build left op right. For division, ensure right is non-zero literal (or subtree forced nonzero). */
    int op = rand() % 4; /* 0:+ 1:- 2:* 3:/ */
    int left_depth = depth - 1;
    int right_depth = depth - 1;
    /* for more variety sometimes make one side shallow */
    if (rand() % 2) left_depth = rand() % depth;
    if (rand() % 2) right_depth = rand() % depth;

    /* left */
    if (!gen_expr(left_depth, false)) return false;
    append_spaces();

    /* operator */
    const char *ops = "+-*/";
    char tmpop[2] = { ops[op], '\0' };
    if (!append_str(tmpop)) return false;
    append_spaces();

    /* right */
    if (op == 3) {
      /* division: make sure right side is non-zero literal or expression forced non-zero.
         To be simple and safe, produce a non-zero literal with some probability, else a non-zero subtree. */
      if (rand() % 3 != 0) {
        /* non-zero literal */
        if (!gen_number_literal(true)) return false;
      } else {
        if (!gen_expr(right_depth, true)) return false;
      }
    } else {
      if (!gen_expr(right_depth, false)) return false;
    }
    return true;
  }
}

static void gen_rand_expr() {
  buf[0] = '\0';
  /* choose random target complexity */
  int depth = 1 + rand() % 4; /* depth 1..4 by default; adjust as needed */
  /* also sometimes produce longer by increasing depth */
  if (rand() % 10 == 0) depth += rand() % 4;

  /* generate until success or buffer would overflow */
  if (!gen_expr(depth, false)) {
    /* reset and fallback to a simple literal */
    buf[0] = '\0';
    gen_number_literal(false);
  }

  /* trim leading/trailing spaces (just in case) */
  char *p = buf;
  while (*p == ' ') p++;
  if (p != buf) memmove(buf, p, strlen(p) + 1);
  int len = strlen(buf);
  while (len > 0 && buf[len-1] == ' ') { buf[len-1] = '\0'; len--; }
}

int main(int argc, char *argv[]) {
  int seed = (int)time(0);
  if (argc > 1 && strcmp(argv[1], "-seed") == 0 && argc > 2) {
    seed = atoi(argv[2]);
  } else if (argc > 1 && atoi(argv[1]) > 0) {
    /* if first arg is number of tests, keep seed random */
  }
  srand(seed);
  fprintf(stderr, "seed=%d\n", seed); /* print seed so results reproducible */

  int loop = 1;
  if (argc > 1) {
    /* handle both: ./gen-expr N  or ./gen-expr -seed S N */
    if (strcmp(argv[1], "-seed") == 0) {
      if (argc > 3) sscanf(argv[3], "%d", &loop);
    } else {
      sscanf(argv[1], "%d", &loop);
    }
  }

  int i;
  for (i = 0; i < loop; i ++) {
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -O2 -w -o /tmp/.expr");
    if (ret != 0) {
      /* compilation failed (should be rare) -- skip this case */
      i--; /* try again to keep count stable */
      continue;
    }

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    unsigned result;
    ret = fscanf(fp, "%u", &result);
    pclose(fp);
    if (ret != 1) {
      /* execution/scan failed -- skip and retry */
      i--;
      continue;
    }

    printf("%u %s\n", result, buf);
  }
  return 0;
}
