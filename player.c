#include "player.h"
#include "chlng.h"
#include <ctype.h>
// #include <cstddef>
// #include <cstdlib>
// #include <cstdlib>
#include "tcp.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

player_t *player_new(void) {
  player_t *new_player = malloc(sizeof(*new_player));
  if (new_player == NULL) {
    fprintf(stderr, "malloc: failed!\n");
    exit(EXIT_FAILURE);
  }

  new_player->finished = false;
  new_player->solved = 0;
  new_player->total = 0;

  chlng_t *new_chlng = chlng_new();
  new_player->chlng = new_chlng;

  return new_player;
}

void player_reset(player_t *p) {
  p->finished = false;
  p->solved = 0;
  p->total = 0;
  chlng_reset(p->chlng);
}

void player_del(player_t *p) {
  player_reset(p);
  chlng_del(p->chlng);
  free(p);
}

int player_fetch_chlng(player_t *p) {
  if (chlng_fetch_text(p->chlng) == -1) {
    fprintf(stderr, "chlng_fetch_text: failed!\n");
    return EXIT_FAILURE;
  }

  if (chlng_hide_word(p->chlng) == -1) {
    fprintf(stderr, "chlng_hide_word: failed!\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int player_get_greeting(char **msg) {
  char *greeting = "M: Guess the missing ____!\n"
                   "M: Send your guess in the form 'R: word\\r\\n'.\n";
  *msg = strdup(greeting);
  if (msg == NULL) {
    fprintf(stderr, "strdup: failed!\n");
    return EXIT_FAILURE;
  }

  return strlen(greeting);
}

int player_get_challenge(player_t *p, char **msg) {
  if (player_fetch_chlng(p) == -1) {
    fprintf(stderr, "player_fetch_chlng: failed!\n");
    return EXIT_FAILURE;
  }

  char *format = "C: %s";
  size_t length = snprintf(NULL, 0, format, p->chlng->text) + 1;
  char *new_msg = malloc(length);
  if (new_msg == NULL) {
    fprintf(stderr, "malloc: failed!\n");
    return EXIT_FAILURE;
  }
  snprintf(new_msg, length, format, p->chlng->text);
  *msg = new_msg;

  if (msg == NULL) {
    fprintf(stderr, "strdup: failed!\n");
    return EXIT_FAILURE;
  }

  return strlen(*msg);
}

int player_post_challenge(player_t *p, char *line, char **msg) {
  if (!p || !line || !msg || !p->chlng || !p->chlng->word) {
    fprintf(stderr, "Invalid input or NULL word!\n");
    return -1; // Invalid input
  }
  if (strcmp(line, "Q:\n") == 0) {
    char *format = "M: You mastered %d/%d challenges. Good bye!";
    size_t length = snprintf(NULL, 0, format, p->solved, p->total) + 1;
    char *new_msg = malloc(length);

    if (new_msg == NULL) {
      fprintf(stderr, "malloc: failed!\n");
      return EXIT_FAILURE;
    }

    snprintf(new_msg, length, format, p->solved, p->total);
    *msg = new_msg;

    player_del(p);
    return 2;
  }
  // Debugging output
  printf("Debug: p->chlng->word: %s\n", p->chlng->word);
  printf("Debug: line: %s\n", line);

  // Trim leading and trailing whitespace from the line
  char *trimmed_line = strdup(line);
  char *end = trimmed_line + strlen(trimmed_line) - 1;
  while (end > trimmed_line && isspace((unsigned char)*end)) {
    end--;
  }
  *(end + 1) = '\0';

  int comparison_result = strcasecmp(trimmed_line, p->chlng->word);

  free(trimmed_line);

  printf("Debug: comparison result: %d\n", comparison_result);

  if (comparison_result == 0) {
    p->solved++;
    p->total++;
    *msg = "Congratulations - challenge passed!\n";
    return 1; // Challenge passed
  } else if (comparison_result != 0) {
    p->total++;
    *msg =
        malloc(strlen("Wrong guess - expected: ") + strlen(p->chlng->word) + 1);
    strcpy(*msg, "Wrong guess - expected: ");
    strcat(*msg, p->chlng->word);
    strcat(*msg, "\n");
    return 1; // Challenge failed
  }
  return EXIT_SUCCESS;
}
