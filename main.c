/*
 * mycom: ECHO application over UART.
 * Usage: mycom <device>   e.g. mycom /dev/ttyAMA0
 *
 * Receives a line, logs it with a timestamp, replies with the same text
 * with the letter case swapped (a<->A) followed by "\r\n".
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_LINE 256

/* ---------- signals ---------- */

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_sig;

static void on_signal(int sig) {
  g_sig = sig;
  g_stop = 1;
}

static int install_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; /* no SA_RESTART: read() is interrupted by a signal */
  if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
    return -1;
  return 0;
}

/* ---------- log ---------- */

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

static FILE* open_log(void) {
  char ts[32];
  char name[64];
  stamp(ts, sizeof(ts), 0);
  snprintf(name, sizeof(name), "%s.log", ts);
  FILE* log = fopen(name, "a");
  if (!log)
    perror(name);
  return log;
}

/* Print the error on screen, write it to the log, return exit code 1. */
static int fail(FILE* log, const char* what, const char* dev) {
  int e = errno; /* save at once, other calls may overwrite it */
  char msg[256];
  snprintf(msg, sizeof(msg), "ERROR: %s %s: %s", what, dev, strerror(e));
  fprintf(stderr, "%s\n", msg);
  log_line(log, msg);
  return 1;
}

/* ---------- port ---------- */

static int setup_port(int fd, struct termios* saved) {
  struct termios tio;
  if (tcgetattr(fd, saved) < 0)
    return -1;
  tio = *saved;
  cfmakeraw(&tio);
  cfsetispeed(&tio, B115200);
  cfsetospeed(&tio, B115200);
  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;
  tio.c_cc[VMIN] = 0;  /* read() returns even with no data... */
  tio.c_cc[VTIME] = 1; /* ...after 0.1 s, so the loop sees g_stop */
  tcflush(fd, TCIOFLUSH);
  return tcsetattr(fd, TCSANOW, &tio);
}

static int write_all(int fd, const char* buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    buf += n;
    len -= (size_t)n;
  }
  return 0;
}

/* ---------- echo ---------- */

static void swap_case(char* s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z')
      *s -= 'a' - 'A';
    else if (*s >= 'A' && *s <= 'Z')
      *s += 'a' - 'A';
  }
}

static int reply_line(int fd, FILE* log, char* line, int truncated) {
  char msg[MAX_LINE + 32];
  snprintf(msg, sizeof(msg), "%s%s", line,
           truncated ? " (line truncated)" : "");
  log_line(log, msg);
  swap_case(line);
  if (write_all(fd, line, strlen(line)) < 0 || write_all(fd, "\r\n", 2) < 0)
    return -1;
  return 0;
}

/* Main loop. Returns 0 on a normal stop, 1 on an error. */
static int run(int fd, FILE* log, const char* dev) {
  char line[MAX_LINE];
  size_t len = 0;
  int truncated = 0;
  int rc = 0;

  while (!g_stop && rc == 0) {
    unsigned char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR)
        continue;
      rc = fail(log, "read error on", dev);
      break;
    }
    for (ssize_t i = 0; i < n && rc == 0; i++) {
      char c = (char)buf[i];
      if (c == '\r' || c == '\n') {
        if (len == 0)
          continue; /* empty line or the second half of \r\n */
        line[len] = '\0';
        if (reply_line(fd, log, line, truncated) < 0)
          rc = fail(log, "write error on", dev);
        len = 0;
        truncated = 0;
      } else if (len < sizeof(line) - 1) {
        line[len++] = c;
      } else {
        truncated = 1;
      }
    }
  }
  return rc;
}

/* ---------- main ---------- */

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: mycom <device>\n");
    return 1;
  }
  const char* dev = argv[1];

  FILE* log = open_log(); /* log first, so every later error is recorded */
  if (!log)
    return 1;
  log_line(log, "started");

  int rc = 0;
  if (install_signals() < 0) {
    rc = fail(log, "cannot install signal handlers for", dev);
  } else {
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
      rc = fail(log, "cannot open", dev);
    } else {
      struct termios saved;
      if (setup_port(fd, &saved) < 0) {
        rc = fail(log, "cannot configure", dev);
      } else {
        log_line(log, "port opened");
        printf("Listening on %s. Press Ctrl+C to stop.\n", dev);
        rc = run(fd, log, dev);
        tcsetattr(fd, TCSANOW, &saved); /* restore the original settings */
      }
      close(fd);
    }
  }

  if (g_sig) {
    char msg[64];
    snprintf(msg, sizeof(msg), "stopped by signal %d", (int)g_sig);
    log_line(log, msg);
    printf("%s\n", msg);
  }
  fclose(log);
  return rc;
}
