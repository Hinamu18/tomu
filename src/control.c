#include <errno.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <poll.h>

#include "backend.h"
#include "control.h"
#include "utils.h"

void help(){
  printf(
    "Usage: tomu [COMMAND] [PATH]\n"
    " Commands:\n\n"

    "   --loop            : loop same sound\n"
    "   --shuffle-loop    : select random audio file and loop\n"
    "   --version         : show version of program\n"
    "   --help            : show help message\n"

    "\nkeys:\n"
    " Space = pause/resume\n"
    " q = quit\n"

    "\nExample: tomu loop [FILE.mp3]\n"
  );
}

struct keybinding { const char *key; void (*handler)(PlayBackState*); };

#define CTRL_KEY(key) (const char[]){key - 'a' + 1 , '\0'}
#define IS_CHAR(c) (c < 127 && c > 31)
#define BACKSPACE (const char[]){ 127 , '\0' }

static const struct keybinding keybindings[] = {
    {" "     ,       playback_toggle},
    {"q"     ,       playback_stop},
    {"\x1b[A",       volume_increase}, // Up
    {"\x1b[B",     	 volume_decrease}, // Down
};

static const int kbds_len = sizeof(keybindings) / sizeof(struct keybinding);

// For interactive player
// TODO: not complete yet & have some bugs
// TODO: use a struct to save keybindings for easier scaling
void *handle_input(void *arg){
  PlayBackState *state = (PlayBackState*)arg;

  struct termios old, raw;
  char key_buf[4] = {0}; // for escape sequences

  tcgetattr(STDIN_FILENO, &old);
  raw = old;

  raw.c_lflag &= ~(ICANON | ECHO);

  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  printf("\033[?25l"); // hide cursor
  fflush(stdout);

  struct pollfd pfd = {
    .fd = STDIN_FILENO,
    .events = POLLIN
  };

  while (state->running){
    // wait 100ms for input
    int ret = poll(&pfd, 1, 100);

    if (ret > 0 && (pfd.revents & POLLIN)) {

        // key press
        int n = read(STDIN_FILENO, key_buf, 1);
        // n shouldn't be zero since we polled successfully
        if (n < 0) { perror("read"); break; }; 

        // check if we have an escape sequence and ready bytes.
        if (key_buf[0] == '\x1b') {
            // TODO: remove the magic number
            int ret = poll(&pfd, 1, 40); // we're not sure of the sequence's size and we don't want to block

            if (ret < 0) {
                perror("poll ecsape sequence");
                break;
            } // fuck off on errors
            if (ret == 1 && (pfd.revents & POLLIN)) read(STDIN_FILENO, key_buf + 1, sizeof(key_buf) - 1); // read into key_buf[1] and forward
        }

        // now we just find the proper keybinding..
        // a hashmap should be used here but allocating mem here is overkill
        for (uint i = 0; i < kbds_len; i++) {
            // a state struct should probably be used but meh
            if (strcmp(key_buf, keybindings[i].key) == 0) keybindings[i].handler(state); 
        }

        if (!state->running) break;
    }

    else if (ret == 0) 
      continue;

    else 
      perror("[W] poll error");
  }

  printf("\033[?25h\r"); // show cursor
  fflush(stdout);

  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  return NULL;
}

void playback_toggle(PlayBackState *state) {
    if (state->paused)
        playback_resume(state);
    else 
        playback_pause(state);
}

void playback_stop(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->paused = 0;
  state->running = 0;
  pthread_cond_broadcast(&state->waitKudasai);
  pthread_mutex_unlock(&state->lock);
}

void volume_increase(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->volume += 0.02f;
  if (state->volume > 1.25f) state->volume = 1.25f;
  pthread_mutex_unlock(&state->lock);
}

void volume_decrease(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->volume -= 0.02f;
  if (state->volume < 0.00f) state->volume = 0.00f;
  pthread_mutex_unlock(&state->lock);
}

void path_handle(const char *path){
	struct stat st;
	if (stat(path, &st)<0 )  goto free;
	if (S_ISDIR(st.st_mode)) shuffle(path);
	if (S_ISREG(st.st_mode)) playback_run(path);
	else goto free;

	return;
free:
	die("FILE: %s",strerror(errno));
}

void shuffle(const char *path){
  DIR *dir = opendir(path);
  struct dirent *entry;
  int count = 0;
  srand(time(NULL));

  if (!dir ) goto free;
  
  while ((entry = readdir(dir)) != NULL ){
    if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) 
		continue;
    count++;
  }

  if (count == 0) goto free;

  int index_rand = rand() % count;
  rewinddir(dir);

  for (int i = 0; i <= index_rand;){
    entry = readdir(dir);
    if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) continue;
    if (i == index_rand){
      char filename[512];
      snprintf(filename, sizeof(filename), "%s/%s", path, entry->d_name);
      playback_run(filename);
      break;
    }
    i++;
  }

  // find a better control flow.. you're running the die function on end.
  // also who the fuck uses two space indentation? it should be four!!!!
  closedir(dir); 
  return;

free:
  closedir(dir);
  die("FILE: %s",strerror(errno));
}
