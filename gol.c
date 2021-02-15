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
pthread_mutex_t MTX;         // Mutex for n_ready's critical sections
pthread_cond_t  MOSI_CV;     // Condition variable master->slave
pthread_cond_t  MISO_CV;     // Condition variable slave->master
int UNIV_SIZE;               // Universe's size
int TOT_THREADS = 4;         // Number of threads, default to 4
bool* T_STATES;              // Array containing threads' status
char UNIV[1000][1000];       // 2D Matrix representing the universe
char TEMP_UNIV[1000][1000];  // Temporary universe for holding next frame
/************************/

typedef struct {  // Arguments for each slave thread:
  int  id;        // Thread id
} ThreadArgs;

int nReady() {
  /*
   * Return number of threads ready.
   */
  int ready_t = 0;  // Number of ready threads
  for (int t = 0; t < TOT_THREADS; t++)
    if (T_STATES[t])
      ready_t++;
  return ready_t;
}

void syncMS(int id) {
  /*
   * Synchronize master and slaves.
   * int id: thread's id of caller, -1 is master
   */
  pthread_mutex_lock(&MTX);
  int ready_t = nReady();
  if (id == -1) {  // Master's critical section
    if (ready_t == TOT_THREADS) {  // If all slaves are ready unleash them
      ready_t = 0;
      for (int t = 0; t < TOT_THREADS; t++)
        T_STATES[t] = false;
      pthread_cond_broadcast(&MOSI_CV);
    }
    while (ready_t != TOT_THREADS) {  // Wait until everyone is ready
      pthread_cond_wait(&MISO_CV, &MTX);
      ready_t = nReady();  // Update number of ready threads
    }
  } else {  // Slave's critical section
    T_STATES[id] = true;  // Slave just finished computing
    ready_t = nReady();
    if (ready_t == TOT_THREADS) {  // When everyone is ready signal master
      pthread_cond_signal(&MISO_CV);
      // Evolve old universe
      for (int y = 0; y < UNIV_SIZE; y++)
        for (int x = 0; x < UNIV_SIZE; x++)
          UNIV[y][x] = TEMP_UNIV[y][x];
    }
    while (T_STATES[id])  // Until unleashed
      pthread_cond_wait(&MOSI_CV, &MTX);  // Wait to be unleashed
  }
  pthread_mutex_unlock(&MTX);
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

void show() {
  /*
   * Print the universe to standard output using ANSI escape codes.
   */
  size_t pts = 0;     // Game points
  printf("\033[H");   // Move the cursor to the top left corner of the screen
  for (int y = 0; y < UNIV_SIZE; y++) {
    for (int x = 0; x < UNIV_SIZE; x++) {
      switch (UNIV[y][x]) {
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

int friends(int c, int r) {
  /*
   * Return the number of neighbour cells alive.
   * int c: cell of interest's column
   * int r: cell of interest's row
   */
  int n_f = 0;  // Initial number of friends
  for (int y = r - 1; y <= r + 1; y++)
    for (int x = c - 1; x <= c + 1; x++) {
      if ((UNIV[(y + UNIV_SIZE) % UNIV_SIZE][(x + UNIV_SIZE) % UNIV_SIZE]) == '1')
        n_f++;  // Found neighbour on adjacent cell
    }
  return UNIV[r][c] == '1' ? n_f - 1 : n_f;  // Don't count self as friend
}

void tick(int x_from, int x_to, int y_from, int y_to) {
  /*
   * Evolve old universe's portion into its next frame.
   * int x_from: x index to start from
   * int x_to: x index to stop at
   * int y_from: y index to start from
   * int y_to: y index to stop at
   */
  for (int y = y_from; y < y_to; y++)
    for (int x = x_from; x < x_to; x++) {
      int n_f = friends(x, y);
      TEMP_UNIV[y][x] = n_f == 3 || (n_f == 2 && UNIV[y][x] == '1') ? '1' : '0';
    }
}

void initUniv() {
  /*
   * Initialize univ reading from "universe.txt"
   */
  FILE *f = fopen("universe.txt", "r");
  if (!f) logErr1("Error: could not open file 'universe.txt'\n");

  char *l = NULL;       // Buffer to hold the line
  size_t l_size = 0;    // Buffer's size in bytes
  ssize_t c_read;       // Characters read, including delimiter, but not \0
  int n_l = UNIV_SIZE;  // Number of lines to read

  // Read file line by line copying its content into univ
  while ((c_read = getline(&l, &l_size, f)) != -1) {
    c_read--;  // Ignore delimiter
    if (c_read > UNIV_SIZE)
      logErr2("Error: line too long in 'universe.txt'.\n", EIO);
    if (c_read < UNIV_SIZE)
      logErr2("Error: line too short in 'universe.txt'.\n", EIO);
    if (n_l < 1)
      logErr2("Error: too many lines in 'universe.txt'.\n", EIO);
    strncpy(UNIV[UNIV_SIZE - n_l], l, UNIV_SIZE);
    strncpy(TEMP_UNIV[UNIV_SIZE - n_l], l, UNIV_SIZE);
    n_l--;  // Decrement lines to read
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
  int width = UNIV_SIZE / TOT_THREADS;
  int x_from = args->id * width;
  int x_to = x_from + width;
  if (args->id == TOT_THREADS - 1)
    x_to = UNIV_SIZE;
  while (true) {
    syncMS(args->id);  // Sync
    tick(x_from, x_to, 0, UNIV_SIZE);
  }
}

void gol() {
  /*
   * Set everything up and start game loop
   */

  pthread_t  t_arr[TOT_THREADS];
  ThreadArgs a_arr[TOT_THREADS];

  // Initialize universe
  initUniv();

  // Initialize threads
  ThreadArgs *a_ptr = a_arr;
  int err;
  for (int t_id = 0; t_id < TOT_THREADS; t_id++) {
    // Init args and spawn thread
    a_ptr->id = t_id;
    T_STATES[t_id] = true;
    if (err = pthread_create(&t_arr[t_id], NULL, slave, (void *)a_ptr))
      logErr2("Error: unable to create thread.\n", err);
    a_ptr++;
  }

  char temp[5];
  // Start game loop
  while (true) {
    usleep(8000);
    show();
    syncMS(-1);  // Sync
  }
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
  if (pthread_mutex_init(&MTX, NULL) != 0)
    logErr1("Error: unable to initialize mutex");
  if (pthread_cond_init (&MOSI_CV, NULL) != 0)
    logErr1("Error: unable to initialize condition variable");
  if (pthread_cond_init (&MISO_CV, NULL) != 0)
    logErr1("Error: unable to initialize condition variable");

  UNIV_SIZE = s;
  TOT_THREADS = n_t;
  T_STATES = malloc(sizeof(bool)*TOT_THREADS);

  // Start the game
  gol();
}
