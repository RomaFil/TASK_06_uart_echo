#include <stdio.h>

int main(int argc, char** argv) {
  (void)argv;
  if (argc < 2) {
    fprintf(stderr, "Usage: mycom <device>\n");
    return 1;
  }
  return 0;
}
