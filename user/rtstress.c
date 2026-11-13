#include "kernel/types.h"
#include "user/user.h"

#define STRESS_RUNS 10

int
main(void)
{
  int passed = 0;

  printf("RT stress test: running rttest %d times\n", STRESS_RUNS);

  for (int i = 0; i < STRESS_RUNS; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("RUN %d: FAIL (fork failed)\n", i + 1);
      continue;
    }

    if (pid == 0) {
      char *argv[] = {"rttest", 0};

      exec("rttest", argv);
      printf("RUN %d: FAIL (exec rttest failed)\n", i + 1);
      exit(1);
    }

    int status = -1;
    int got = wait(&status);

    if (got == pid && status == 0) {
      passed++;
      printf("RUN %d: PASS\n", i + 1);
    } else {
      printf("RUN %d: FAIL (pid=%d, got=%d, status=%d)\n", i + 1, pid, got,
             status);
    }
  }

  printf("\nRT STRESS RESULT: %d/%d PASS\n", passed, STRESS_RUNS);
  exit(passed == STRESS_RUNS ? 0 : 1);
}