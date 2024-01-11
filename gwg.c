/*
 * gwg.c --
 * use gcc -o xhesi chlng.c gwg.c player.c tcp-accept.c tcp-close.c tcp-listen.c
 * tcp-read-write.c tcp-read.c tcp-write.c
 * ./xhesi post_nr -t/-f
 * NOTE I AM USING THE DEFAULT BEHAVIOR THAT FOR PERFORMANCE REASONS YOU CANNOT
 * CONNECT TWICE TO THE SAME PORT SO IF YOU ARE GETTING AN ERROR CONNECT TO THE
 * PORT, USE EXAMPLE PORT  ./xhesi 9769 -t WHICH HAS NOT BEEN USED BY THIS
 * PROGRAM, THIS ISSUE SHOULD NOT BE THE CASE, BUT JUST LEETING YOU KNOW
 */
#define _POSIX_C_SOURCE 200809L
#include "chlng.h"
#include "player.h"
#include "tcp.h"
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef struct {
  char *address;
  int fd;
} listen_t;

int game_logic(int cfd) {

  player_t *p;
  char *msg;
  int rc;

  p = player_new();
  if (!p) {
    return EXIT_FAILURE;
  }

  // get the greeting message from the player
  rc = player_get_greeting(&msg);
  if (rc > 0) {
    // send the greeting message to the client
    if (tcp_write(cfd, msg, strlen(msg)) == -1) {
      player_del(p);
      (void)free(msg);
      return EXIT_FAILURE;
    }
    (void)free(msg);
  }

  while (!(p->finished)) {
    char *line = NULL;
    size_t linecap = 0;

    rc = player_get_challenge(p, &msg);

    if (rc > 0) {
      // Send the challenge message to the client
      if (tcp_write(cfd, msg, strlen(msg)) == -1) {
        player_del(p);
        (void)free(msg);
        return EXIT_FAILURE;
      }
      (void)free(msg);
    }

    // Read the client's response
    ssize_t bytes_read = getline(&line, &linecap, fdopen(cfd, "r"));
    if (bytes_read <= 0) {
      // If no bytes are read, post a default challenge message and break out of
      // the loop
      rc = player_post_challenge(p, "Q: ", &msg);
      if (tcp_write(cfd, msg, strlen(msg)) == -1) {
        // Clean up resources and return failure if writing to the client fails
        player_del(p);
        (void)free(msg);
        return EXIT_FAILURE;
      }
      break;
    }

    // Post the client's response as the player's answer
    rc = player_post_challenge(p, line, &msg);
    if (rc > 0) {
      // Send the response to the client
      if (tcp_write(cfd, msg, strlen(msg)) == -1) {
        // Clean up resources and return failure if writing to the client fails
        player_del(p);
        (void)free(msg);
        return EXIT_FAILURE;
      }
      // (void)free(msg);
    }
    (void)free(line);
  }

  // Clean up resources and return success
  player_del(p);
  return EXIT_SUCCESS;
}

// Function to handle default mode (no threading or forking)
void mode_default(int cfd) {
  // Call the main application logic
  game_logic(cfd);
  (void)close(cfd);
}
void *mode_thread_go(void *arg) {
  int cfd = (intptr_t)arg;

  // Call default mode within the thread
  mode_default(cfd);
  return NULL;
}

// Function to create a thread and execute the default mode
void mode_thread(int cfd) {
  int rc;
  pthread_t tid;

  // Create a thread and pass the client socket to it
  rc = pthread_create(&tid, NULL, mode_thread_go, (void *)(intptr_t)cfd);
  if (rc != 0) {
    fprintf(stderr, "pthread_create: failed\n");
  }
}

void mode_fork(int cfd) {
  pid_t pid;
  pid = fork();

  if (pid == -1) {
    fprintf(stderr, "fork: failed!\n");
    exit(EXIT_FAILURE);
  }

  // In the child process, execute the default mode and exit
  if (pid == 0) {
    mode_default(cfd);
    exit(EXIT_SUCCESS);
  }
  (void)close(cfd);
}

static void mainloop(listen_t *listeners, void (*mode)(int)) {
  while (1) {
    fd_set fdset;
    FD_ZERO(&fdset);
    int maxfd = 0;

    for (listen_t *l = listeners; l->address; l++) {
      if (l->fd > 0) {
        FD_SET(l->fd, &fdset);
        maxfd = (l->fd > maxfd) ? l->fd : maxfd;
      }
    }

    if (maxfd == 0) {
      break;
    }

    if (select(maxfd + 1, &fdset, NULL, NULL, NULL) == -1) {
      (void)fprintf(stderr, "select: failed!\n");
      exit(EXIT_FAILURE);
    }

    for (listen_t *l = listeners; l->address; l++) {
      if ((l->fd > 0) && (FD_ISSET(l->fd, &fdset))) {
        int cfd = tcp_accept(l->fd);
        if (cfd == -1) {
          (void)fprintf(stderr, "accept: failed!\n");
        }

        mode(cfd);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  listen_t *iface, interfaces[] = {{.address = "0.0.0.0"},
                                   {.address = "::"},
                                   {.address = NULL}};

  int opt, port = 12345;
  void (*mode)(int) = mode_fork;
  opterr = 0;
  while ((opt = getopt(argc, argv, "tfp:")) != -1) {
    switch (opt) {
    case 't':
      mode = mode_thread;
      break;
    case 'f':
      mode = mode_fork;
      break;
    case 'p':
      // port = atoi(optarg);
      // port = atoi(argv[optind]);
      port = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Usage: %s [-t] [-f] [-p port_number]!\n", argv[0]);
      break;
    }
  }

  for (iface = interfaces; iface->address; iface++) {
    char port_char[5];
    // sprintf(port_char, "%d", port);

    // iface->fd = tcp_listen(iface->address, port_char);
    iface->fd = tcp_listen(iface->address, argv[optind]);

    if (iface->fd == -1) {
      (void)fprintf(stderr, "listening on %s port %d failed!\n", iface->address,
                    port);
    }
  }

  mainloop(interfaces, mode);

  return EXIT_SUCCESS;
}
