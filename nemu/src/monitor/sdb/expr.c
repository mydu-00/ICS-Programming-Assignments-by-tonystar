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
#include <memory/vaddr.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
  TK_NOTYPE = 256, TK_EQ, TK_NEQ, TK_NUM, TK_HEX, TK_REG, TK_NEG, TK_DEREF
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {
  {" +", TK_NOTYPE},           // spaces
  {"\\(", '('},
  {"\\)", ')'},
  {"==", TK_EQ},
  {"!=", TK_NEQ},
  {"0[xX][0-9a-fA-F]+", TK_HEX},
  {"[0-9]+", TK_NUM},
  {"\\$[a-zA-Z][a-zA-Z0-9]*", TK_REG},
  {"\\+", '+'},
  {"\\-", '-'},
  {"\\*", '*'},
  {"/", '/'},
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[128] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        int ttype = rules[i].token_type;
        if (ttype == TK_NOTYPE) {
          /* skip spaces */
          break;
        }

        Token *tk = &tokens[nr_token++];
        tk->type = ttype;
        int len = substr_len < (int)sizeof(tk->str) - 1 ? substr_len : (int)sizeof(tk->str) - 1;
        strncpy(tk->str, substr_start, len);
        tk->str[len] = '\0';

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }
  /* post process tokens: identify unary - and unary * (dereference) */
  for (int j = 0; j < nr_token; j++) {
    if (tokens[j].type == '-' ) {
      if (j == 0 || (tokens[j-1].type != TK_NUM && tokens[j-1].type != TK_HEX && tokens[j-1].type != TK_REG && tokens[j-1].type != ')' )) {
        tokens[j].type = TK_NEG;
      }
    } else if (tokens[j].type == '*') {
      if (j == 0 || (tokens[j-1].type != TK_NUM && tokens[j-1].type != TK_HEX && tokens[j-1].type != TK_REG && tokens[j-1].type != ')' )) {
        tokens[j].type = TK_DEREF;
      }
    }
  }

  return true;
}

/* helper: find matching parenthesis from position p to q.
 * returns index of the matching ')' for '(' at pos p, or -1 */
static int find_parentheses(int p, int q) {
  int cnt = 0;
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') cnt++;
    else if (tokens[i].type == ')') {
      cnt--;
      if (cnt == 0) return i;
    }
  }
  return -1;
}

/* map register string like "$eax" to value */
static word_t reg_str2val(const char *s, bool *ok) {
  *ok = true;
  
  /* 通用 pc 寄存器 */
  if (strcmp(s, "$pc") == 0) return cpu.pc;
  
#ifdef CONFIG_ISA_x86
  /* x86 registers */
  if (strcmp(s, "$eax") == 0) return reg_l(0);
  if (strcmp(s, "$ecx") == 0) return reg_l(1);
  if (strcmp(s, "$edx") == 0) return reg_l(2);
  if (strcmp(s, "$ebx") == 0) return reg_l(3);
  if (strcmp(s, "$esp") == 0) return reg_l(4);
  if (strcmp(s, "$ebp") == 0) return reg_l(5);
  if (strcmp(s, "$esi") == 0) return reg_l(6);
  if (strcmp(s, "$edi") == 0) return reg_l(7);
  if (strcmp(s, "$eip") == 0) return cpu.pc;
#elif defined(CONFIG_ISA_riscv32) || defined(CONFIG_ISA_riscv64)
  /* RISC-V registers */
  /* x0-x31 registers */
  for (int i = 0; i < 32; i++) {
    char reg_name[8];
    snprintf(reg_name, sizeof(reg_name), "$x%d", i);
    if (strcmp(s, reg_name) == 0) return cpu.gpr[i];
  }
  
  /* ABI names */
  if (strcmp(s, "$zero") == 0) return cpu.gpr[0];
  if (strcmp(s, "$ra") == 0) return cpu.gpr[1];
  if (strcmp(s, "$sp") == 0) return cpu.gpr[2];
  if (strcmp(s, "$gp") == 0) return cpu.gpr[3];
  if (strcmp(s, "$tp") == 0) return cpu.gpr[4];
  if (strcmp(s, "$t0") == 0) return cpu.gpr[5];
  if (strcmp(s, "$t1") == 0) return cpu.gpr[6];
  if (strcmp(s, "$t2") == 0) return cpu.gpr[7];
  if (strcmp(s, "$s0") == 0 || strcmp(s, "$fp") == 0) return cpu.gpr[8];
  if (strcmp(s, "$s1") == 0) return cpu.gpr[9];
  if (strcmp(s, "$a0") == 0) return cpu.gpr[10];
  if (strcmp(s, "$a1") == 0) return cpu.gpr[11];
  if (strcmp(s, "$a2") == 0) return cpu.gpr[12];
  if (strcmp(s, "$a3") == 0) return cpu.gpr[13];
  if (strcmp(s, "$a4") == 0) return cpu.gpr[14];
  if (strcmp(s, "$a5") == 0) return cpu.gpr[15];
  if (strcmp(s, "$a6") == 0) return cpu.gpr[16];
  if (strcmp(s, "$a7") == 0) return cpu.gpr[17];
  if (strcmp(s, "$s2") == 0) return cpu.gpr[18];
  if (strcmp(s, "$s3") == 0) return cpu.gpr[19];
  if (strcmp(s, "$s4") == 0) return cpu.gpr[20];
  if (strcmp(s, "$s5") == 0) return cpu.gpr[21];
  if (strcmp(s, "$s6") == 0) return cpu.gpr[22];
  if (strcmp(s, "$s7") == 0) return cpu.gpr[23];
  if (strcmp(s, "$s8") == 0) return cpu.gpr[24];
  if (strcmp(s, "$s9") == 0) return cpu.gpr[25];
  if (strcmp(s, "$s10") == 0) return cpu.gpr[26];
  if (strcmp(s, "$s11") == 0) return cpu.gpr[27];
  if (strcmp(s, "$t3") == 0) return cpu.gpr[28];
  if (strcmp(s, "$t4") == 0) return cpu.gpr[29];
  if (strcmp(s, "$t5") == 0) return cpu.gpr[30];
  if (strcmp(s, "$t6") == 0) return cpu.gpr[31];
#endif
  
  /* unknown register */
  *ok = false;
  return 0;
}

/* evaluate tokens between indices p..q (inclusive) */
static word_t eval(int p, int q, bool *success) {
  if (p > q) { *success = false; return 0; }
  if (p == q) {
    Token *tk = &tokens[p];
    if (tk->type == TK_NUM) {
      *success = true;
      return (word_t)strtoul(tk->str, NULL, 10);
    } else if (tk->type == TK_HEX) {
      *success = true;
      return (word_t)strtoul(tk->str, NULL, 0);
    } else if (tk->type == TK_REG) {
      bool ok;
      word_t v = reg_str2val(tk->str, &ok);
      *success = ok;
      return v;
    } else {
      *success = false;
      return 0;
    }
  }

  /* parentheses */
  if (tokens[p].type == '(' && find_parentheses(p, q) == q) {
    return eval(p + 1, q - 1, success);
  }

  /* search for dominant operator (lowest precedence) */
  int op = -1;
  int min_prec = 1000;
  int level = 0;
  for (int i = p; i <= q; i++) {
    int t = tokens[i].type;
    if (t == '(') { level++; continue; }
    if (t == ')') { level--; continue; }
    if (level > 0) continue;

    int prec = 100;
    if (t == TK_EQ || t == TK_NEQ) prec = 1;
    else if (t == '+' || t == '-') prec = 2;
    else if (t == '*' || t == '/') prec = 3;
    else if (t == TK_NEG || t == TK_DEREF) prec = 4; /* unary highest precedence */

    if (prec <= min_prec) {
      min_prec = prec;
      op = i;
    }
  }

  if (op == -1) { *success = false; return 0; }

  /* unary operators */
  if (tokens[op].type == TK_NEG) {
    bool ok2 = false;
    word_t val = eval(op + 1, q, &ok2);
    if (!ok2) { *success = false; return 0; }
    *success = true;
    return (word_t)(-(int64_t)val);
  } else if (tokens[op].type == TK_DEREF) {
    bool ok2 = false;
    word_t addr = eval(op + 1, q, &ok2);
    if (!ok2) { *success = false; return 0; }
    *success = true;
    return vaddr_read(addr, 4);
  }

  /* binary operators */
  bool ok1 = false, ok2 = false;
  word_t val1 = eval(p, op - 1, &ok1);
  word_t val2 = eval(op + 1, q, &ok2);
  if (!ok1 || !ok2) { *success = false; return 0; }

  int t = tokens[op].type;
  switch (t) {
    case '+': *success = true; return val1 + val2;
    case '-': *success = true; return val1 - val2;
    case '*': *success = true; return val1 * val2;
    case '/': if (val2 == 0) { *success = false; return 0; } *success = true; return val1 / val2;
    case TK_EQ: *success = true; return (val1 == val2);
    case TK_NEQ: *success = true; return (val1 != val2);
    default: *success = false; return 0;
  }
}

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }
  if (nr_token == 0) { *success = false; return 0; }

  word_t result = eval(0, nr_token - 1, success);
  return result;
}
