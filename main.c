#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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

static void stamp(char* buf, size_t n, int with_ms) {
  struct timespec ts;
  struct tm tm;
  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm);
  if (with_ms) {
    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(buf, n, "%s.%03ld", base, ts.tv_nsec / 1000000L);
  } else {
    strftime(buf, n, "%Y-%m-%d_%H-%M-%S", &tm);
  }
}

static void log_line(FILE* log, const char* text) {
  char ts[48];
  stamp(ts, sizeof(ts), 1);
  fprintf(log, "[%s] %s\n", ts, text);
  fflush(log);
}

int main(int argc, char** argv) {
  (void)argv;
  if (argc < 2) {
    fprintf(stderr, "Usage: mycom <device>\n");
    return 1;
  }
  char ts[32];
  char name[64];
  stamp(ts, sizeof(ts), 0);
  snprintf(name, sizeof(name), "%s.log", ts);
  FILE* log = fopen(name, "a");
  if (!log) {
    perror(name);
    return 1;
  }
  log_line(log, "started");
  if (install_signals() < 0) {
    perror("sigaction");
    return 1;
  }
  printf("Running, pid %d. Press Ctrl+C.\n", getpid());
  while (!g_stop)
    usleep(100000);
  printf("Stopped by signal %d\n", (int)g_sig);
  printf("Running, pid %d. Press Ctrl+C.\n", getpid());
  while (!g_stop)
    usleep(100000);
  printf("Stopped by signal %d\n", (int)g_sig);
  char msg[64];
  snprintf(msg, sizeof(msg), "stopped by signal %d", (int)g_sig);
  log_line(log, msg);
  fclose(log);
  return 0;
}
