#ifndef PLAYER_H
#define PLAYER_H

#include "chlng.h"
#include <stdbool.h>
typedef struct player {
  int solved;     /* correctly solved challenges */
  int total;      /* total number of challenges */
  bool finished;  /* true if we are done */
  chlng_t *chlng; /* current challenge */
} player_t;

/* Allocate a new player. */
extern player_t *player_new(void);

/* Reset the internal state of the player. */
extern void player_reset(player_t *p);

/* Delete a player and all its resources. */
extern void player_del(player_t *p);

/* Allocate a new challenge for the player. */
extern int player_fetch_chlng(player_t *p);

/* Retrieve a greeting message. */
extern int player_get_greeting(char **msg);

/* Retrieve the challenge message. */
extern int player_get_challenge(player_t *p, char **msg);

/* Post a message to the player and retrieve the response message. */
extern int player_post_challenge(player_t *p, char *guess, char **msg);

#endif
