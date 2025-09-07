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

/* recursively generate expression. depth controls size, nonzero_required indicates we must produce non-zero value. */
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
    /* unary minus: ensure a space after '-' to avoid forming '--' with previous/succeeding '-' */
    if (!append_str("- ")) return false;
    return gen_expr(depth - 1, nonzero_required);
  } else {
    /* binary operator */
    /* build left op right. For division, ensure right is non-zero literal (or subtree forced nonzero). */
    int op = rand() % 4; /* 0:+ 1:- 2:* 3:/ */
    int left_depth = depth - 1;
    int right_depth = depth - 1;
    if (rand() % 2) left_depth = rand() % depth;
    if (rand() % 2) right_depth = rand() % depth;

    /* left */
    if (!gen_expr(left_depth, false)) return false;
    /* operator: insert with spaces around to avoid token merging (e.g. "--", "++") */
    const char *ops = "+-*/";
    char opbuf[4];
    snprintf(opbuf, sizeof(opbuf), " %c ", ops[op]);
    if (!append_str(opbuf)) return false;

    /* right */
    if (op == 3) {
      /* division: make sure right side is non-zero literal or expression forced non-zero.
         To be simple and safe, produce a non-zero literal with high probability, else a non-zero subtree. */
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
  int depth = 1 + rand() % 4;
  if (rand() % 10 == 0) depth += rand() % 4;

  if (!gen_expr(depth, false)) {
    buf[0] = '\0';
    gen_number_literal(false);
  }

  /* trim leading/trailing spaces */
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
  fprintf(stderr, "seed=%d\n", seed);

  int loop = 1;
  if (argc > 1) {
    if (strcmp(argv[1], "-seed") == 0) {
      if (argc > 3) sscanf(argv[3], "%d", &loop);
    } else {
      sscanf(argv[1], "%d", &loop);
    }
  }

  for (int i = 0; i < loop; i ++) {
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -O2 -w -o /tmp/.expr");
    if (ret != 0) {
      i--;
      continue;
    }

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    unsigned result;
    ret = fscanf(fp, "%u", &result);
    pclose(fp);
    if (ret != 1) {
      i--;
      continue;
    }

    printf("%u %s\n", result, buf);
  }
  return 0;
}
