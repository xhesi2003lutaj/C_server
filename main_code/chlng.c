#include "chlng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

chlng_t *chlng_new(void) {
  chlng_t *new_chlng = malloc(sizeof(*new_chlng));
  if (new_chlng == NULL) {
    fprintf(stderr, "malloc: failed!\n");
    exit(EXIT_FAILURE);
  }

  new_chlng->text = NULL;
  new_chlng->word = NULL;

  return new_chlng;
}

void chlng_reset(chlng_t *c) {
  free(c->text);
  free(c->word);
  c->text = NULL;
  c->word = NULL;
}

void chlng_del(chlng_t *c) {
  chlng_reset(c);
  free(c);
}

int chlng_fetch_text(chlng_t *c) {
  char *buffer = NULL;
  char **arglist;
  arglist = (char **)malloc(3 * sizeof(char *));
  arglist[0] = "fortune";
  arglist[1] = "-s";
  arglist[2] = NULL;
  int fd[2];

  if (pipe(fd) == -1) {
    perror("Error creating the pipe");
    // return EXIT_FAILURE;
  }
  pid_t pid = fork();
  if (pid == -1) {
    perror("Error in fork");
  }
  if (pid == 0) {
    close(fd[0]);
    dup2(fd[1], STDOUT_FILENO);
    execvp(arglist[0], arglist);
    close(fd[1]);
  } else {
    close(fd[1]);
    buffer = (char *)malloc(1024 * sizeof(char *));
    while (read(fd[0], buffer, 1024)) {
    }
  }
  close(fd[0]);
  // Store the fetched text in the challenge structure
  c->text = strdup(buffer);
  if (c->text == NULL) {
    fprintf(stderr, "strdup: failed!\n");
    return EXIT_FAILURE;
  }

  // printf("%s\n", buffer);
  // c->text = buffer;
  return EXIT_SUCCESS;
}

char *getRandomWord(char *sentence, char **maskedSentence) {
  // Copy the sentence since strtok modifies the original string
  char *copy = strdup(sentence);

  // Tokenize the sentence into words
  char *word = strtok(copy, " ");
  char **words = NULL;
  int count = 0;

  while (word != NULL) {
    words = realloc(words, (count + 1) * sizeof(char *));
    words[count] = strdup(word);
    count++;
    word = strtok(NULL, " ");
  }

  // Check if there are any words
  if (count == 0) {
    free(copy);
    return NULL;
  }

  // Generate a random index
  int randomIndex = rand() % count;

  // Get the random word
  char *randomWord = strdup(words[randomIndex]);

  // Create a copy of the original sentence for modification
  *maskedSentence = strdup(sentence);

  // Find the position of the random word in the masked sentence
  char *replacePtr = *maskedSentence;

  for (int i = 0; i < randomIndex; i++) {
    replacePtr = strstr(replacePtr, words[i]) + strlen(words[i]);
  }

  int replaceLength = strlen(randomWord);
  memcpy(replacePtr, "___", 3);
  memcpy(replacePtr + 3, replacePtr + replaceLength,
         strlen(replacePtr + replaceLength) + 1);

  // Free allocated memory
  for (int i = 0; i < count; i++) {
    free(words[i]);
  }
  free(words);
  free(copy);

  return randomWord;
}
int chlng_hide_word(chlng_t *c) {

  char *maskedSentence;
  c->word = getRandomWord(c->text, &maskedSentence);
  c->text = maskedSentence;
  return EXIT_SUCCESS;
}
