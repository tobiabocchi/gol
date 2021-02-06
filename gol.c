// Copyright (c) 2021, Tobia Bocchi, All rights reseved.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

/*** Global Variables ***/
pthread_mutex_t n_ready_mtx;  // Mutex for n_ready's critical sections
pthread_cond_t  mosi_cv;      // Condition variable master->slave
pthread_cond_t  miso_cv;      // Condition variable slave->master
int n_ready = 0;              // Number of slave threads ready
/************************/

typedef struct {  // Arguments for each slave thread:
  int  id;        // Thread id
  int  *n_t;      // Total number of threads
  int  *s;        // Universe's size
  char **univ;    // Old frame
} ThreadArgs;

void syncMS(bool master, int *n_slaves) {
  /*
   * Synchronize master and slaves.
   * bool master: whether the master or a slave has control
   * int *n_slaves: the total number of slaves
   */
  pthread_mutex_lock(&n_ready_mtx);
  if (master) {  // Master's critical section
    if (n_ready == *n_slaves) {  // If all slaves are ready unleash them
      n_ready = 0;
      pthread_cond_broadcast(&mosi_cv);
    }
    while (n_ready != *n_slaves) {// Wait until everyone is ready
      pthread_cond_wait(&miso_cv, &n_ready_mtx);
    }
  }
  else {  // Slave's critical section
    n_ready++;  // Mark self as ready
    if (n_ready == *n_slaves)  // When everyone is ready signal master
      pthread_cond_signal(&miso_cv);
    pthread_cond_wait(&mosi_cv, &n_ready_mtx);  // Wait to be unleashed
  }
  pthread_mutex_unlock(&n_ready_mtx);
}

void logErr1(char *str) {
  /*
   * Log error and exit.
   * char *str: message to log
   */
  perror(str);
  exit(EXIT_FAILURE);
}

void logErr2(char *str, int err) {
  /*
   * Set errno, log error and exit.
   * char *str: message to log
   * int err: errno error code
   */
  errno = err;
  logErr1(str);
}

void show(int s, char** univ) {
  /*
   * Print the universe to standard output using ANSI escape codes.
   * int s: universe's size
   * char** univ: 2D matrix holding universe
   */
  return;
  size_t pts = 0;     // Game points
  printf("\033[H");   // Move the cursor to the top left corner of the screen
  for (int y = 0; y < s; y++) {
    for (int x = 0; x < s; x++) {
      switch (univ[y][x]) { // TODO: fix pointer handling here
        case '0':  // Dead cell
          printf("  ");
          break;
        case '1':  // Alive cell
          printf("\033[07m  \033[m");  // Invert color and print cell
          pts++;
          break;
        default:
          logErr2("Error: invalid value in universe cell", EINVAL);
          break;
      }
    }
    printf("\033[E");  // Move the cursor to the next line
  }
  printf("\033[2KScore: %i\n", pts);  // Clear entire line and print score
  fflush(stdout);
}

int friends(int s, char** univ, int c, int r) {
  /*
   * Return the number of neighbour cells alive.
   * int s: universe's size
   * char **univ: 2D matrix holding universe
   * int c: cell of interest's column
   * int r: cell of interest's row
   */
  int n_f = 0;  // Initial number of friends
  for (int y = r - 1; y <= r + 1; y++)
    for (int x = c - 1; x <= c + 1; x++)
      if ((univ[(y + s) % s][(x + s) % s]) == '1')
        n_f++;  // Found neighbour on adjacent cell
  return univ[r][c] == '1' ? n_f - 1 : n_f;  // Don't count self as friend
}

void tick(int s, int x_from, int x_to, int y_from, int y_to, char** o_univ) {
  /*
   * Evolve old universe's portion into its next frame.
   * int s: universe's size
   * int x_from: x index to start from
   * int x_to: x index to stop at
   * int y_from: y index to start from
   * int y_to: y index to stop at
   * char** o_univ: 2D matrix holding universe
   */
  char n_univ[y_to - y_from][x_to - x_from];  // Store next state here
  for (int y = y_from; y < y_to; y++)
    for (int x = x_from; x < x_to; x++) {
      int n_f = friends(s, o_univ, x, y);
      // Update cell's status in the new universe
      n_univ[y][x] = n_f == 3 || (n_f == 2 && o_univ[y][x] == '1') ? '1' : '0';
    }
  // Evolve old universe
  for (int y = y_from; y < y_to; y++)
    for (int x = x_from; x < x_to; x++)
      o_univ[y][x] = n_univ[y][x];
}

void initUniv(int s, char** univ) {
  /*
   * Initialize univ reading from "universe.txt"
   * int s: universe's size
   * char** o_univ: 2D matrix holding universe
   */
  FILE *f = fopen("universe.txt", "r");
  if (!f) logErr1("Error: could not open file 'universe.txt'\n");

  char *l = NULL;     // Buffer to hold the line
  size_t l_size = 0;  // Buffer's size in bytes
  ssize_t c_read;     // Characters read, including delimiter, but not \0
  int n_l = s;        // Number of lines to read

  // Read file line by line copying its content into univ
  while ((c_read = getline(&l, &l_size, f)) != -1) {
    c_read--; // Ignore delimiter
    if (c_read > s)   // TODO(Tobia): use EUCLEAN instead?
      logErr2("Error: line too long in 'universe.txt'.\n", EIO);
    if (c_read < s)
      logErr2("Error: line too short in 'universe.txt'.\n", EIO);
    if (n_l < 1)
      logErr2("Error: too many lines in 'universe.txt'.\n", EIO);
    strncpy(*univ, l, s);
    univ++;   // Advance pointer to the next row
    n_l--;    // Decrement lines to read
  }

  if (n_l)
    logErr2("Error: too little lines in 'universe.txt'.\n", EIO);

  // Close file and free memory
  if (fclose(f))
    logErr1("Error: could not close file 'universe.txt'.\n");
  free(l);
}

void *slave(void *a) {
  /*
   * Slave's code, sync operations and compute next frame's portions
   * void *a: pointer to a ThreadArgs struct
   */
  ThreadArgs *args = (ThreadArgs *)a;
  int width = *args->s / *args->n_t;
  int x_from = args->id * width;
  int x_to = x_from + width;
  if (args->id == *args->n_t - 1)
    x_to = *(args->s);
  printf("Slave %i renders from %i to %i. w=%i\n", args->id, x_from, x_to, width);
  while (true) {
    syncMS(false, args->n_t); // Sync
    tick(*args->s, x_from, x_to, 0, *args->s, args->univ);
  }
}

void setup(int s, int n_t, pthread_t *t_arr, ThreadArgs *a_arr, char **univ) {
  /*
   * Set up global variables and everything needed for the game
   * int s: universe's size
   * int n_t: the total number of slaves
   * pthread_t *t_arr: array of threads
   * ThreadArgs *a_arr: array of structs with thread's args
   * char** o_univ: 2D matrix holding universe
   */
  // Initialize mutex and condition variable objects
  if(pthread_mutex_init(&n_ready_mtx, NULL) != 0)
    logErr1("Error: unable to initialize mutex");
  if(pthread_cond_init (&mosi_cv, NULL) != 0)
    logErr1("Error: unable to initialize condition variable");
  if(pthread_cond_init (&miso_cv, NULL) != 0)
    logErr1("Error: unable to initialize condition variable");

  ThreadArgs *a_ptr = a_arr;
  int err;
  for (int t_id = 0; t_id < n_t; t_id++) {
    // Init args and spawn thread
    a_ptr->n_t  = &n_t;
    a_ptr->id   = t_id;
    a_ptr->s    = &s;
    a_ptr->univ = univ;
    if(err = pthread_create(&t_arr[t_id], NULL, slave, (void *)a_ptr))
      logErr2("Error: unable to create thread.\n", err);
    a_ptr++;
  }
}

void gol(int s, int n_t) {
  /*
   * Set everything up and start game loop
   * int s: universe's size
   * int n_t: the total number of slaves
   */

  pthread_t  t_arr[n_t];
  ThreadArgs a_arr[n_t];
  char** univ;

  univ = malloc(s * sizeof(char*));
  if (univ == NULL)
    logErr2("Error: malloc failed.\n", ENOMEM);
  for (int y = 0; y < s; y++)
    if ((univ[y] = malloc(s * sizeof(char))) == NULL)
      logErr2("Error: malloc failed.\n", ENOMEM);
  initUniv(s, univ);

  // Set everything up
  // setup(s, n_t, t_arr, a_arr, univ);

  ThreadArgs *a_ptr = a_arr;
  int err;
  for (int t_id = 0; t_id < n_t; t_id++) {
    // Init args and spawn thread
    a_ptr->n_t  = &n_t;
    a_ptr->id   = t_id;
    a_ptr->s    = &s;
    a_ptr->univ = univ;
    if(err = pthread_create(&t_arr[t_id], NULL, slave, (void *)a_ptr))
      logErr2("Error: unable to create thread.\n", err);
    a_ptr++;
  }

  // Start game loop
  while(true) {
    usleep(5000);
    show(s, univ);
    syncMS(true, &n_t); // Sync
  }

  pthread_exit(NULL);
  exit(EXIT_SUCCESS);
}

int main(int c, char **v) {
  /*
   * Parse args and start the game
   */
  if (c == 1) {  // Print usage and exit
    printf("Conway's Game of Life\n");
    printf("Usage:\n%s <size>\n%s <size> <n_threads>\n", v[0], v[0]);
    exit(EXIT_SUCCESS);
  }

  if (c > 3)
    logErr2("Error: too many arguments passed.\n", E2BIG);

  // Init size and n_threads from args
  int s = atoi(v[1]), n_t = 4;  // n_treads default = 4
  if (c == 3)
    n_t = atoi(v[2]);
  if (s < 1 || n_t < 1)
    logErr2("Error: size and n_threads must be > 0.\n", EINVAL);

  // Initialize mutex and condition variable objects
  if(pthread_mutex_init(&n_ready_mtx, NULL) != 0)
    logErr1("Error: unable to initialize mutex");
  if(pthread_cond_init (&mosi_cv, NULL) != 0)
    logErr1("Error: unable to initialize condition variable");
  if(pthread_cond_init (&miso_cv, NULL) != 0)
    logErr1("Error: unable to initialize condition variable");

  // Start the game
  gol(s, n_t);
}
