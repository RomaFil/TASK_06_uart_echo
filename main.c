#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_sig;
static void on_signal(int sig) {
  g_sig = sig;
  g_stop = 1; /* більше нічого в обробнику: printf і malloc там небезпечні */
}

static int install_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags =
      0; /* без SA_RESTART: read() перерветься і цикл побачить прапорець */
  if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
    return -1;
  return 0;
}

int main(int argc, char** argv) {
  (void)argv;
  if (argc < 2) {
    fprintf(stderr, "Usage: mycom <device>\n");
    return 1;
  }
  if (install_signals() < 0) {
    perror("sigaction");
    return 1;
  }
  printf("Running, pid %d. Press Ctrl+C.\n", getpid());
  while (!g_stop)
    usleep(100000);
  printf("Stopped by signal %d\n", (int)g_sig);
  return 0;
}
