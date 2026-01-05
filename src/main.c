#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include "backend.h"
#include "control.h"
// #include "utils.h"

#define VERSION "0.0.5"

int main(int argc, char *argv[]){
  if (argc < 2 ){
    printf("Usage: %s [FILE]\n", basename((argv[0])));
    return 0;
  }

  const char *command = argv[1];
  const char *filename = argv[argc - 1];

  if (strcmp("--loop", command) == 0 )
    while (1) path_handle(filename);

  else if (strcmp("--shuffle-loop", command) == 0 )
    while (1) shuffle(filename);

  else if (strcmp("--help", command) == 0 )
    help();

  else if (strcmp("--version", command) == 0 )
    printf("%s\n", VERSION);

  else
	path_handle(filename);

  return 0;
}
