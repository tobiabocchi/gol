// Copyright (c) 2021, Tobia Bocchi, All rights reseved.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

typedef unsigned u;

void logErr1(char *str) {
  /*
   * Log error and exit
   */
  perror(str);
  exit(EXIT_FAILURE);
}

void logErr2(char *str, int err) {
  /*
   * Set errno, log error and exit.
   */
  errno = err;
  logErr1(str);
}

void show(int w, int h, char univ[][w]) {
  /*
   * Print the universe to standard output using ANSI escape codes.
   */
  size_t pts = 0;  // Game points
  printf("\033[H");  // Move the cursor to the top left corner of the screen
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++)
      switch (univ[y][x]) {
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
    printf("\033[E");  // Move the cursor to the next line
  }
  printf("\033[2KScore: %i", pts);  // Clear entire line and print score
  fflush(stdout);
}

int friends(int w, int h, char univ[][w], int c, int r) {
  /*
   * Given a universe and a position (col,row)
   * Return the number of cells alive next to that position
   */
  int n_f = 0;  // Initial number of friends
  for (int y = r - 1; y <= r + 1; y++)
    for (int x = c - 1; x <= c + 1; x++)
      if ((univ[(y + h) % h][(x + w) % w]) == '1')
        n_f++;  // Found neighbour on adjacent cell
  return univ[r][c] == '1' ? n_f - 1 : n_f;  // Don't count self as friend
}

void tick(int w, int h, char o_univ[][w]) {
  /*
   * Evolve old universe into its next frame
   */
  char n_univ[h][w];  // New universe for next frame
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int n_f = friends(w, h, o_univ, x, y);
      // Update cell's status in the new universe
      n_univ[y][x] = n_f == 3 || (n_f == 2 && o_univ[y][x] == '1') ? '1' : '0';
    }
  // Evolve old universe
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      o_univ[y][x] = n_univ[y][x];
}

void initUniv(int w, int h, char univ[][w]) {
  /*
   * Initialize univ reading from "universe.txt"
   */
  FILE *f = fopen("universe.txt", "r");
  if (!f) logErr1("Error: could not open file 'universe.txt'\n");

  char *l = NULL;     // Buffer to hold the line
  size_t l_size = 0;  // Buffer's size in bytes
  ssize_t c_read;     // Characters read, including delimiter, but not \0

  // Read file line by line copying its content into univ
  while ((c_read = getline(&l, &l_size, f)) != -1) {
    if (c_read - 1 != w)  // Ignore delimiter
      logErr2("Error: wrong line width in 'universe.txt'.\n", EIO);
    // TODO(Tobia): use EUCLEAN instead?
    if (h <= 0)
      logErr2("Error: too many lines in 'universe.txt'.\n", EIO);
    strncpy(*univ, l, w);
    univ++;  // Advance pointer to the next row
    h--;
  }

  if (h)
    logErr2("Error: too little lines in 'universe.txt'.\n", EIO);

  // Close file and free memory
  if (fclose(f))
    logErr1("Error: could not close file 'universe.txt'.\n");
  free(l);
}

void gol(int w, int h) {
  /*
   * Initialize the universe then enter the game loop
   */
  char univ[h][w];
  initUniv(w, h, univ);

  while (1) {
    // Print universe
    show(w, h, univ);
    // Advance game's state by 1 frame
    tick(w, h, univ);
    // Wait 50 millisecond between frames
    usleep(50000);
  }
}

int main(int c, char **v) {
  /*
   * Parse args and start the game
   */
  if (c == 1) {  // Print usage and exit
    printf("Usage:\n%s <size>\n%s <width> <height>\n", v[0], v[0]);
    exit(EXIT_SUCCESS);
  }

  if (c > 3)
    logErr2("Error: too many arguments passed.\n", E2BIG);

  // Init w & h and start the game
  int w, h;
  if (c == 2)
    w = h = atoi(v[1]);
  else
    w = atoi(v[1]), h = atoi(v[2]);
  if (h < 1 || w < 1)
    logErr2("Error: Both width and height must be greater than 0.\n", EINVAL);
  gol(w, h);
}
