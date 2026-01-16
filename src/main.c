#include <stdio.h>
#include <string.h>

// #include "control.h"
#include "utils.h"

#define PROG_NAME "tomu"
#define PROG_VER "0.0.10"

int main(int argc, char *argv[])
{
  // 1. check user what want
  if ( argc == 1 ){
    printf("Usage: %s [FILE.mp3]\n", PROG_NAME); 
    return 0;
  }

  char *option = argv[1];
  char *path = argv[argc - 1];

  // 2. See what the user wants with "--" and handle it
  if ( argv[1][0] == '-' && argv[1][1] == '-' ){

    if ( strcmp("--loop", option ) == 0 ){
      path_handle(path, true);
      return 0;
    }

    // TODO later
    // else if ( strcmp("--shuffle", option) == 0 ){
    //   if ( argc < 3 ){
    //     printf("[T]: Please Provide what to shuffle\n");
    //     return 1;
    //   }
    //
    //   char *target = argv[2];
    //
    //   // shuffle with playlist
    //   // meaning random with change in same path
    //   // tomu --shuffle playlist [DIR]
    //   if (strcmp("playlist", target) == 0)
    //     shuffle(path, true);
    //
    //   else
    //     printf("[T]: Unknown Shuffle target '%s'\n", target);
    //
    //   return 0;
    // }

    else if ( strcmp("--help", option) == 0 ){
      help();
      return 0;
    }

    else if ( strcmp("--version", option) == 0 ){
      printf("Tomu: %s\n", PROG_VER);
      return 0;
    }

    else {
      printf("[T]: unknown option '%s'\n", option);
      return 0;
    }
  }

  // 3. No options? Just handle the path (check file or directory)  
  path_handle(path, false);
  return 0;
}
