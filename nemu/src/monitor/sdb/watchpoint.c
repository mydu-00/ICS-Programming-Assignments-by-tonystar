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

#include "sdb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define NR_WP 32

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;
  
  /* 添加缺失的字段 */
  char expr[256];
  word_t val;
  bool enabled;
} WP;

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
    wp_pool[i].expr[0] = '\0';
    wp_pool[i].val = 0;
    wp_pool[i].enabled = false;
  }

  head = NULL;
  free_ = wp_pool;
}

/* Allocate a new watchpoint for the given expression string. */
WP* new_wp(const char *e) {
  if (!free_) {
    printf("No free watchpoint.\n");
    return NULL;
  }

  WP *wp = free_;
  free_ = free_->next;

  wp->next = head;
  head = wp;

  strncpy(wp->expr, e, sizeof(wp->expr) - 1);
  wp->expr[sizeof(wp->expr) - 1] = '\0';
  wp->enabled = true;

  bool ok = false;
  wp->val = expr(wp->expr, &ok);
  if (!ok) {
    /* evaluation failed: remove from active list and put back to free list */
    if (head == wp) {
      head = wp->next;
    } else {
      WP *p = head;
      while (p && p->next != wp) p = p->next;
      if (p) p->next = wp->next;
    }
    wp->next = free_;
    free_ = wp;
    wp->enabled = false;
    printf("Failed to evaluate expression: %s\n", e);
    return NULL;
  }

  printf("Watchpoint %d set: %s = 0x%lx\n", wp->NO, wp->expr, (unsigned long)wp->val);
  return wp;
}

/* Delete a watchpoint by its number. Returns true on success. */
bool delete_wp(int no) {
  WP *p = head, *prev = NULL;
  while (p) {
    if (p->NO == no) break;
    prev = p;
    p = p->next;
  }
  if (!p) {
    printf("No watchpoint number %d.\n", no);
    return false;
  }

  /* unlink from active list */
  if (prev) prev->next = p->next;
  else head = p->next;

  /* reset and push to free list */
  p->expr[0] = '\0';
  p->val = 0;
  p->enabled = false;
  p->next = free_;
  free_ = p;

  printf("Deleted watchpoint %d.\n", no);
  return true;
}

/* Print all active watchpoints */
void info_wp() {
  if (!head) {
    printf("No watchpoints.\n");
    return;
  }
  printf("Num Expr                          Value\n");
  printf("-------------------------------------------------\n");
  WP *p = head;
  while (p) {
    if (p->enabled)
      printf("%-3d %-30s 0x%016lx\n", p->NO, p->expr, (unsigned long)p->val);
    p = p->next;
  }
}

/* Check all watchpoints; if any changed, report and update stored value. */
bool check_wp() {
  WP *p = head;
  bool triggered = false;
  while (p) {
    if (p->enabled) {
      bool ok = false;
      word_t v = expr(p->expr, &ok);
      if (ok && v != p->val) {
        printf("Watchpoint %d triggered: %s\n", p->NO, p->expr);
        printf("Old value = 0x%lx\nNew value = 0x%lx\n", (unsigned long)p->val, (unsigned long)v);
        p->val = v;
        triggered = true;
      } else if (!ok) {
        printf("Failed to evaluate watchpoint %d: %s\n", p->NO, p->expr);
      }
    }
    p = p->next;
  }
  return triggered;
}

/* helper to find watchpoint by number (may return NULL) */
WP* find_wp(int no) {
  WP *p = head;
  while (p) {
    if (p->NO == no) return p;
    p = p->next;
  }
  return NULL;
}
