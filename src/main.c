#include <stdio.h>
#include <string.h>

#include "control.h"
#include "utils.h"

#define PROG_NAME "tomu"
#define PROG_VER "0.0.8"

int main(int argc, char *argv[])
{
  if (argc < 2){
    printf("Usage: %s [File.mp3]\n", PROG_NAME);
    return 0;
  }

  const char *arg = argv[1];
  const char *filename = argv[argc - 1];

  if (argv[1][0] == '-' && argv[1][1] == '-') {
    if (strcmp("--loop", arg) == 0)
      path_handle(filename, true);

    else if (strcmp("--help", arg) == 0)
      help();

    else if (strcmp("--version", arg) == 0)
      printf("%s\n", PROG_VER);

    else 
      printf("[T] Unknown Arg '%s'\n", arg);
  } else path_handle(filename, false);

  return 0;
}
