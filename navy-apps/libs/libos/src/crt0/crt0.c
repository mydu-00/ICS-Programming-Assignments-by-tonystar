#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

int main(int argc, char *argv[], char *envp[]);
extern char **environ;
void call_main(uintptr_t *args) {
  // args: [argc][argv[0..argc-1]][NULL][envp[0..]][NULL]
  int argc = (int)args[0];
  char **argv = (char **)&args[1];
  char **envp = (char **)&args[1 + (size_t)argc + 1];

  environ = envp;
  exit(main(argc, argv, envp));
  assert(0);
}
