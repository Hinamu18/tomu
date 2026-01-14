#include <errno.h>
#include <stdio.h>
#include <sys/poll.h>
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
    "   --version         : show version of program\n"
    "   --help            : show help message\n"

    "\nkeys:\n"
    " (Space) = pause/resume\n"
    " (q) = quit\n"
    " (-) = decrease volume\n"
    " (+) = increase volume\n"
    " (↑/→) = audio seek forward +5s/1m\n"
    " (←/↓) = audio seek backward -5s/1m\n"

    "\nExample: tomu loop [FILE.mp3]\n"
  );
}

struct keybinding { const char *key; void (*handler)(PlayBackState*); };

#define CTRL_KEY(key) (const char[]){key - 'a' + 1 , '\0'} // remove this when you move the code to socket function

static const struct keybinding keybindings[] = {
    {" "     ,       playback_toggle},
    {"q"     ,       playback_stop},
    {"-"     ,       volume_decrease},
    {"+"     ,       volume_increase},
    {"\x1b[A",       seek_forward_min}, // Up arrow
    {"\x1b[B",     	 seek_backward_min}, // Down arrow
    {"\x1b[D",       seek_backward_sec},   // Left arrow
    {"\x1b[C",       seek_forward_sec},    // Right arrow
};

static const int kbds_len = sizeof(keybindings) / sizeof(struct keybinding);

// For interactive player
void *handle_input(void *arg){
  PlayBackState *state = (PlayBackState*)arg;

  struct termios old, raw;

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
    // wait 80ms for input
    int ret = poll(&pfd, 1, 80);

    if (!state->running) break;

    if (ret > 0 && (pfd.revents & POLLIN)) {
        char key_buf[4] = {0}; // for escape sequences

        // key press
        int n = read(STDIN_FILENO, key_buf, 1);
        // n shouldn't be zero since we polled successfully
        if (n < 0) { perror("read"); break; }; 

        // check if we have an escape sequence and ready bytes.
        if (key_buf[0] == '\x1b') {
            // TODO: remove the magic number
            int ret = poll(&pfd, 1, -1); // we're not sure of the sequence's size and we don't want to block

            if (ret < 0) {
                perror("poll ecsape sequence");
                break;
            } // fuck off on errors
            if (ret == 1 && (pfd.revents & POLLIN)) {
                int n = read(STDIN_FILENO, key_buf + 1, sizeof(key_buf) - 1);
                if (n < 0) {
                    perror("read escape sequence");
                    break;
                }
            }
        }

        // now we just find the proper keybinding..
        // a hashmap should be used here but allocating mem here is overkill
        for (uint i = 0; i < kbds_len; i++) {
            if (strcmp(key_buf, keybindings[i].key) == 0) keybindings[i].handler(state); 

        }

        if (!state->running) break; // leave if there's nothing playing
    }

    else if (ret == 0) {
      continue;
    }

    else 
      perror("[F] poll error");
  }

  printf("\033[?25h\r"); // show cursor
  fflush(stdout);

  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  return NULL;
}

// functions for playback
// fn toggle pause/resume
inline void playback_toggle(PlayBackState *state) {
    if (state->paused)
        playback_resume(state);
    else 
        playback_pause(state);
}

inline void playback_pause(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
    state->paused = 1;
  pthread_mutex_unlock(&state->lock);
}

inline void playback_resume(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
    state->paused = 0;
    pthread_cond_broadcast(&state->wait_cond);
  pthread_mutex_unlock(&state->lock);
}

// Stops playback and wakes any waiting threads
inline void playback_stop(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
    state->paused = 0;
    state->running = 0;
    pthread_cond_broadcast(&state->wait_cond);
  pthread_mutex_unlock(&state->lock);
}

// Requests a seek forward by 5 seconds
void seek_forward_sec(PlayBackState *state)
{
  pthread_mutex_lock(&state->lock);
    if (!state->seek_request){
      state->seek_request = 1;
      state->seek_target = +5000000; // +5 sec in microseconds
      pthread_cond_broadcast(&state->wait_cond);
    }
  pthread_mutex_unlock(&state->lock);
}


// Requests a seek forward by 1 min
void seek_forward_min(PlayBackState *state)
{
  pthread_mutex_lock(&state->lock);
    if (!state->seek_request){
      state->seek_request = 1;
      state->seek_target = +60000000; // +60 sec in microseconds
      pthread_cond_broadcast(&state->wait_cond);
    }
  pthread_mutex_unlock(&state->lock);
}

// Requests a seek backward by 5 seconds
void seek_backward_sec(PlayBackState *state)
{
  pthread_mutex_lock(&state->lock);
    if (!state->seek_request){
      state->seek_request = 1;
      state->seek_target = -5000000; // -5 sec in microseconds
      pthread_cond_broadcast(&state->wait_cond);
    }
  pthread_mutex_unlock(&state->lock);
}

// Requests a seek backward by 1 min
void seek_backward_min(PlayBackState *state)
{
  pthread_mutex_lock(&state->lock);
    if (!state->seek_request){
      state->seek_request = 1;
      state->seek_target = -60000000; // -60 sec in microseconds
      pthread_cond_broadcast(&state->wait_cond);
    }
  pthread_mutex_unlock(&state->lock);
}

// =================================================================


// functions for handle a volume of playback audio
// fn change value of a control volume
inline void volume_increase(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
    state->volume += 0.02f;
    if (state->volume > 1.26f) state->volume = 1.26f;
  pthread_mutex_unlock(&state->lock);
}

inline void volume_decrease(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
    state->volume -= 0.02f;
    if (state->volume < 0.00f) state->volume = 0.00f;
  pthread_mutex_unlock(&state->lock);
}
// ===================================================================

void shuffle(const char *path, uint loop){
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

  for (int i = 0; i <= index_rand; i++){
    entry = readdir(dir);
    if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) continue;
    if (i == index_rand){
      char filename[512];
      snprintf(filename, sizeof(filename), "%s/%s", path, entry->d_name);
      playback_run(filename, loop);
      break;
    }
  }

  // find a better control flow.. you're running the die function on end.
  closedir(dir); 
  return;

free:
  closedir(dir);
  die("FILE: %s",strerror(errno));
}
