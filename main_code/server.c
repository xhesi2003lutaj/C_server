/*
	CO-562-A
	hw8_p1.c
	Henri Sota
	h.sota@jacobs-university.de
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <event2/event.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include "tcp.h"

typedef struct game {
    char* sentence;
    char* hidden_word;
    int rounds;
    int success;
} game_t;

typedef struct clnt {
    evutil_socket_t fd;
    struct event* event;
    struct clnt* next;
    struct game* game;
} clnt_t;

// Keep track of correct execution or failure of server
int status = EXIT_SUCCESS;

// Keep track of clients connected in a linked list
clnt_t* clients = NULL;

// Base event
struct event_base* evb;

static const char* progname = "server";
const int READ_CHUNK_SIZE = 256;

game_t* game_new() {
    game_t* new_game = (game_t*) calloc(1, sizeof(game_t));

    if (new_game == NULL) {
        fprintf(stderr, "Memory allocation for new game was unsuccessful!\n");
        status = EXIT_FAILURE;
        return NULL;
    }

    new_game->sentence = (char*) calloc(64, sizeof(char));

    if (new_game->sentence == NULL) {
        fprintf(stderr, "Memory allocation for sentence in game was unsuccessful!\n");
        status = EXIT_FAILURE;
        return NULL;
    }

    new_game->hidden_word = NULL;

    new_game->rounds = 0;
    new_game->success = 0;

    return new_game;
}

clnt_t* clnt_new() {
    clnt_t* new_client = (clnt_t*) calloc(1, sizeof(clnt_t));

    if (new_client == NULL) {
        fprintf(stderr, "Memory allocation for new client was unsuccessful!\n");
        status = EXIT_FAILURE;
        return NULL;
    }

    game_t* new_game = game_new();

    if (new_game == NULL) {
        fprintf(stderr, "Error during allocation for new game!\n");
        free(new_client);
        return NULL;
    }

    // Check if there are any clients connected
    if (clients == NULL) {
        // Set next of new client to NULL as there is no other client in the list
        new_client->next = NULL;
    } else {
        // Set next of new client to the old head of the clients list
        new_client->next = clients;
    }

    // Set clients linked list to have the new client as head
    clients = new_client;

    new_client->game = new_game;

    return new_client;
}

// Handle tcp_close() given a certain file descriptor
int tcp_close_handler(int fd) {
    int close_status = 0;

    // Repeat tcp_close() in case of signal interrupt (EINTR)
    do {
        close_status = tcp_close(fd);

        if (close_status == 0)
            break;
        else {
            if (errno != EINTR) {
                fprintf(stderr, "Error during tcp_close()! (%d)\n", errno);
                return EXIT_FAILURE;
            }
        }
    } while (1);

    return 0;
}

// Deallocate the game_t struct connected to a client
void free_game_handler(game_t* game) {
    free(game->sentence);
    free(game->hidden_word);
    free(game);
}

// Deallocate the clnt_t struct connected to a client
void free_clnt_handler(clnt_t* client) {
    free_game_handler(client->game);
    free(client);
}

char* select_and_hide_word(char* sentence) {
    // Calculate number of words and tokenize the sentence
    const char* sep = " .,:;|`!?/_-\t\n\r\v\f";
    int count = 0, chosen_word_len = 0, chosen_word_pos = 0;
    size_t len_word = 0;
    char* cur = sentence;

    // Save the word that is hidden from the sentence
    char* chosen_word = NULL;

    // Initialize RNG with NULL time
    srand(time(NULL));

    // Move past the initial separators if there are any
    cur += strspn(cur, sep);

    // Calculate the number of words in the sentence
    while ((len_word = strcspn(cur, sep)) > 0) {
        count++;
        // Move past the word
        cur += len_word;
        // Move past the separators
        cur += strspn(cur, sep);
    }

    // Move the cursor to the beginning of the sentence again
    cur = sentence;

    // Move past the initial separators if there are any
    cur += strspn(cur, sep);

    // Choose a random word position in the sentence
    chosen_word_pos = rand() % count;

    // Iterate through the words of the sentence until hitting the chosen one
    for (int i = 0; i < count; i++) {
        if (i == chosen_word_pos) {
            // Calculate the length of the word to be hidden
            chosen_word_len = strcspn(cur, sep);

            // Allocate memory to save the hidden word in the game struct later
            chosen_word = (char*) malloc(sizeof(char) * (chosen_word_len + 1));

            if (chosen_word == NULL) {
                fprintf(stderr, "Memory allocation for tokens was unsuccessful!\n");
                status = EXIT_FAILURE;
                return NULL;
            }

            // Copy the word and append null terminator
            strncpy(chosen_word, cur, chosen_word_len);
            chosen_word[chosen_word_len] = '\0';

            // Replace the word in sentence with underscores
            memset(cur, '_', chosen_word_len);

            // Stop iterating after saving the word
            break;
        } else {
            // Move past current word
            cur += strcspn(cur, sep);
            // Move past separators
            cur += strspn(cur, sep);
        }
    }
    
    return chosen_word;
}

void clnt_write(clnt_t* client, const char* code) {
    char* buf = (char*) calloc(128, sizeof(char));
    int bytes_written = 0;

    // Formulate message to send to the client depending on the code
    if (strcmp(code, "send_challenge") == 0) {
        strcat(buf, "C: ");
        strcat(buf, client->game->sentence);
    } else if (strcmp(code, "correct_answer") == 0) {
        strcat(buf, "O: Congratulation - challenge passed!\n");
    } else if (strcmp(code, "incorrect_answer") == 0) {
        sprintf(buf, "F: Wrong guess - expected: %s\n", client->game->hidden_word);
    } else if (strcmp(code, "quit") == 0) {
        sprintf(buf, "M: You mastered %d/%d challenges. Good bye!\n", client->game->success, client->game->rounds);
    } else if (strcmp(code, "undefined") == 0) {
        strcat(buf, "M: You have inputted something undefined!\n");
    }

    // Write formulated message to the client file descriptor
    bytes_written = tcp_write(client->fd, buf, strlen(buf));

    if (bytes_written < 0) {
        fprintf(stderr, "tcp_write() to fd %d was unsuccessful! (%d)\n", client->fd, errno);
        status = EXIT_FAILURE;
    }

    free(buf);
}

void frtn_read(evutil_socket_t evfd, short evwhat, void *evarg) {
    clnt_t* client = (clnt_t*) evarg;
    char buf[READ_CHUNK_SIZE];
    char* hidden_word = NULL;
    ssize_t bytes_read = 0;

    while (1) {
        bytes_read = read(evfd, buf, sizeof(buf));

        if (bytes_read < 0) {
            // Repeat read() in case of signal interrupt (EINTR)
            if (errno != EINTR) {
                fprintf(stderr, "Error during read()! (%d)\n", errno);
                status = EXIT_FAILURE;
                return;
            } else continue;
        } else break;
    }

    // Append null terminator to the bytes read
    buf[bytes_read] = '\0';

    // Copy the sentence to the game struct of the client
    strncpy(client->game->sentence, buf, bytes_read + 1);

    // Save the word hidden in the client game's sentence
    hidden_word = select_and_hide_word(client->game->sentence);

    if (hidden_word == NULL) {
        fprintf(stderr, "Couldn't successfully find a word to hide!\n");
        status = EXIT_FAILURE;
        return;
    }

    // Deallocate memory for last the hidden word
    if (client->game->hidden_word != NULL) {
        free(client->game->hidden_word);
    }

    // Set the hidden word of the game to be the chosen hidden word
    client->game->hidden_word = hidden_word;

    // Send the challenge to the client
    clnt_write(client, "send_challenge");

    if (status == EXIT_FAILURE) {
        fprintf(stderr, "Failed to write to client!\n");
    }
}

int start_fortune(clnt_t* client) {
    // Keep track of result of close() calls
    int close_status = 0;

    // Keep track of the pipe
    int pfd[2];

    if (pipe(pfd) == -1) {
        fprintf(stderr, "Pipe creation was unsuccessful!\n");
        status = EXIT_FAILURE;
        return status;
    }

    struct event* fortune_event = event_new(evb, pfd[0], EV_READ, frtn_read, client);
    event_add(fortune_event, NULL);

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "Forking failed!\n");
        status = EXIT_FAILURE;
        return status;
    }

    if (pid == 0) {
        do {
            // Copy the file descriptor in order to use them interchangeably
            int dup2_ret = dup2(pfd[1], STDOUT_FILENO);

            if (dup2_ret == STDOUT_FILENO) {
                // Success in pipe duplication
                break;
            } else if (dup2_ret == -1) {
                // Repeat dup2() in case of signal interrupt (EINTR)
                if (errno != EINTR) {
                    fprintf(stderr, "Error during dup2()! (%d)\n", errno);
                    status = EXIT_FAILURE;
                    return status;
                }
            }
        } while (1);

        // Repeat close() in case of signal interrupt (EINTR)
        do {
            // Close read end of the pipe for child
            close_status = close(pfd[0]);

            if (close_status == 0)
                break;
            else {
                if (errno != EINTR) {
                    fprintf(stderr, "Error during close()! (%d)\n", errno);
                    status = EXIT_FAILURE;
                    return status;
                }
            }
        } while (1);

        // Repeat close() in case of signal interrupt (EINTR)
        do {
            // Close write end of the pipe for child
            close_status = close(pfd[1]);

            if (close_status == 0)
                break;
            else {
                if (errno != EINTR) {
                    fprintf(stderr, "Error during close()! (%d)\n", errno);
                    status = EXIT_FAILURE;
                    return status;
                } else continue;
            }
        } while (1);

        // Static allocation of the command
        char* command[5] = {
            "fortune",
            "-n",
            "32",
            "-s",
            NULL
        };

        // Execute command
        execvp(command[0], command);
        fprintf(stderr, "Execution of command was unsuccessful! (%d)\n", errno);
        status = EXIT_FAILURE;
        return status;
    } else {
        // Repeat close() in case of signal interrupt (EINTR)
        do {
            // Close write end of the pipe for parent
            close_status = close(pfd[1]);

            if (close_status == 0)
                break;
            else {
                if (errno != EINTR) {
                    fprintf(stderr, "Error during close()! (%d)\n", errno);
                    status = EXIT_FAILURE;
                    return status;
                }
            }
        } while (1);
    }

    return EXIT_SUCCESS;
}

void clnt_del(clnt_t* client) {
    // Check in case no client is present
    if (clients == NULL) {
        fprintf(stderr, "Cannot delete due to no client being connected!\n");
        status = EXIT_FAILURE;
        return;
    }

    int cfd = client->fd;

    event_del(client->event);

    int res_tcp_close = tcp_close_handler(cfd);
    
    if (res_tcp_close == EXIT_FAILURE) {
        return;
    }

    // Check if the client to be delete is at the head of the clients list
    if (client == clients) {
        // Set head of clients list to the next client
        clients = client->next;
        free_clnt_handler(client);
        return;
    } else {
        // Keep track of cursor and previous client to cursor
        clnt_t* curr_client = clients;
        clnt_t* previous_client = NULL;

        // Iterate through the clients
        while (client) {
            // Check if cursor is pointing at the client to be deleted
            if (curr_client == client) {
                // Set previous client to point at next client of cursor
                previous_client->next = curr_client->next;
                free_clnt_handler(curr_client);
                return;
            } else previous_client = curr_client;
            // Move cursor to the next client
            curr_client = curr_client->next;
        }
    }

    // In case execution arrives here, no matching client was successfully found
    fprintf(stderr, "No client to be deleted was found!\n");
    status = EXIT_FAILURE;
}

void clnt_read(evutil_socket_t evfd, short evwhat, void *evarg) {
    const char* sep = " .,:;|`!?/_-\t\n\r\v\f";
    char* buf = (char*) calloc(READ_CHUNK_SIZE, sizeof(char));
    ssize_t bytes_read = 0;
    clnt_t* client = (clnt_t*) evarg;

    while (1) {
        bytes_read = tcp_read(evfd, buf, READ_CHUNK_SIZE);

        if (bytes_read < 0) {
            // Repeat read() in case of signal interrupt (EINTR)
            if (errno != EINTR) {
                fprintf(stderr, "Error during tcp_read()! (%d)\n", errno);
                status = EXIT_FAILURE;
                return;
            } else continue;
        }
        
        break;
    }

    char* response_token_ptr = strstr(buf, "R:");
    char* quit_token_ptr = strstr(buf, "Q:");

    if (response_token_ptr == buf) {
        // Handle response to round question
        char* cur = response_token_ptr;
        char* response = (char*) calloc(64, sizeof(char));

        // Move past word
        cur += strcspn(cur, sep);
        // Move past separators
        cur += strspn(cur, sep);

        // Calculate the length of the proposed word by the client
        int len_response_word = strcspn(cur, sep);

        // Copy word from the current position in the read message to response
        strncpy(response, cur, len_response_word);

        // Increment the number of rounds played
        client->game->rounds++;

        if (strcmp(response, client->game->hidden_word) == 0) {
            // Send message of correct proposed word
            clnt_write(client, "correct_answer");

            if (status == EXIT_FAILURE) {
                fprintf(stderr, "Failed to write to client!\n");
                return;
            }

            // Increment the number of rounds won
            client->game->success++;
        } else {
            // Send message of incorrect proposed word
            clnt_write(client, "incorrect_answer");

            if (status == EXIT_FAILURE) {
                fprintf(stderr, "Failed to write to client!\n");
                return;
            }
        }

        free(response);

        int res_fortune = start_fortune(client);

        if (res_fortune == EXIT_FAILURE) {
            fprintf(stderr, "Error while setting up fortune for client!\n");
        }
    } else if (quit_token_ptr == buf) {
        // Send message of disconnection to client
        clnt_write(client, "quit");

        if (status == EXIT_FAILURE) {
            fprintf(stderr, "Failed to write to client!\n");
            return;
        }
        
        event_free(client->event);

        clnt_del(client);

        if (status == EXIT_FAILURE) {
            fprintf(stderr, "Unsuccessful client deletion!\n");
        }
    } else {
        // Send message to client when response did not comply to the protocol
        clnt_write(client, "undefined");

        if (status == EXIT_FAILURE) {
            fprintf(stderr, "Failed to write to client!\n");
            return;
        }
    }

    free(buf);
}

void clnt_join(evutil_socket_t evfd, short evwhat, void *evarg) {
    int cfd = 0;
    clnt_t* client;
    struct event_base* evb = evarg;

    char* message = "M: Guess the missing ____!\nM: Send your guess in the for 'R: word\\r\\n'.\n";

    ssize_t bytes_written = 0;

    // Accept the incoming client connection
    cfd = tcp_accept(evfd);
    
    if (cfd == -1) {
        fprintf(stderr, "Couldn't connect client! (%d)\n", errno);
        status = EXIT_FAILURE;
        return;
    }

    int res_fcntl = 0;

    // Repeat fcntl() in case of signal interrupt (EINTR)
    do {
        // Set the client file descriptor to be O_NONBLOCK
        res_fcntl = fcntl(cfd, F_SETFL, O_NONBLOCK);

        if (res_fcntl == 0)
            break;
        else {
            if (errno != EINTR) {
                fprintf(stderr, "Error during fcntl()! (%d)\n", errno);
                status = EXIT_FAILURE;
                return;
            }
        }
    } while (1);

    // Allocate the clnt_t struct
    client = clnt_new();

    if (client == NULL) {
        fprintf(stderr, "Error while setting up client!\n");
        return;
    }

    client->fd = cfd;
    client->event = event_new(evb, cfd, EV_READ | EV_PERSIST, clnt_read, client);
    
    int res_event_add = event_add(client->event, NULL);

    if (res_event_add == -1) {
        fprintf(stderr, "Event add failed! (%d)\n", errno);
        status = EXIT_FAILURE;
        return;
    }

    // Write the initial message to the client
    bytes_written = tcp_write(cfd, message, strlen(message));

    if (bytes_written < 0) {
        fprintf(stderr, "tcp_write() to fd %d was unsuccessful! (%d)\n", cfd, errno);
        status = EXIT_FAILURE;
        return;
    }

    // Run child process executing fortune and handle pipe
    int res_fortune = start_fortune(client);

    if (res_fortune == EXIT_FAILURE) {
        fprintf(stderr, "Error while setting up fortune for client!\n");
    }
}

int main(int argc, char* argv[]) {
    int fd;
    struct event* ev;
    const char* interfaces[] = {"0.0.0.0", "::", NULL};

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", progname);
        exit(EXIT_FAILURE);
    }

    evb = event_base_new();

    if (!evb) {
        fprintf(stderr, "Base event creation failed!\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; interfaces[i]; i++) {
        fd = tcp_listen(interfaces[i], argv[1]);

        if (fd < 0) {
            fprintf(stderr, "tcp_listen() on interface |%s| failed! (%d)\n", interfaces[i], errno);
            continue;
        }

        ev = event_new(evb, fd, EV_READ | EV_PERSIST, clnt_join, evb);
        
        int res_event_add = event_add(ev, NULL);

        if (res_event_add == -1) {
            fprintf(stderr, "Event add failed! (%d)\n", errno);
            status = EXIT_FAILURE;
            return status;
        }
    }

    if (event_base_loop(evb, 0) == -1) {
        fprintf(stderr, "Event Base Loop failed! (%d)\n", errno);
        event_base_free(evb);
        exit(EXIT_FAILURE);
    }

    event_base_free(evb);

    int res_tcp_close = tcp_close_handler(fd);

    if (res_tcp_close == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
