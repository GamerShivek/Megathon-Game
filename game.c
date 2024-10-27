#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define PORT 8080
#define MAX_PLAYERS 3
#define MAX_SPELLS 6
#define BUFFER_SIZE 1024
#define MAX_HP 200

typedef struct {
    int socket;
    int hp;
    int spells_cast;
    int frozen_rounds;
    int active; // Indicates if player can still act
} Player;

void send_to_all_players(Player players[], int player_count, const char *message) {
    for (int i = 0; i < player_count; i++) {
        if (players[i].socket != -1) {
            send(players[i].socket, message, strlen(message), 0);
        }
    }
}

void send_hp_status(Player players[], int player_count) {
    char hp_message[BUFFER_SIZE] = "Current HP status:\n";
    for (int i = 0; i < player_count; i++) {
        char line[50];
        snprintf(line, sizeof(line), "Player %d: %d HP\n", i + 1, players[i].hp);
        strcat(hp_message, line);
    }
    send_to_all_players(players, player_count, hp_message);
}

void send_turn_notification(Player *player, int player_id) {
    char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "It's your turn, Player %d!\n", player_id + 1);
    send(player->socket, message, strlen(message), 0);
}

void send_initial_instructions(Player *player, int player_id) {
    char instructions[BUFFER_SIZE] =
        "Welcome! Here’s how to cast spells:\n"
        " - Spell 1: Attack a target for 30 damage. Syntax: 'cast 1 [target_id]'\n"
        " - Spell 2: Heal yourself by a quarter of your current HP. Syntax: 'cast 2'\n"
        " - Spell 3: Freeze a target for 2 rounds, costing 20 HP. Syntax: 'cast 3 [target_id]'\n"
        " - Spell 4: Boost your HP based on half your HP drain. Syntax: 'cast 4'\n"
        "Wait for your turn and follow the syntax above. Good luck!\n";
    send(player->socket, instructions, strlen(instructions), 0);
}

void handle_cast_spell(Player *caster, Player *target, int spell_id, Player players[], int player_count) {
    char message[BUFFER_SIZE];
    if (caster->hp <= 0 || !caster->active) {
        snprintf(message, BUFFER_SIZE, "Player %ld is out of HP and cannot act.\n", caster - players + 1);
        send(caster->socket, message, strlen(message), 0);
        return;
    }

    if (caster == target && (spell_id == 1 || spell_id == 3)) {
        snprintf(message, BUFFER_SIZE, "You cannot cast this spell on yourself. Please choose another target.\n");
        send(caster->socket, message, strlen(message), 0);
        return;
    }

    switch (spell_id) {
        case 1: 
            target->hp -= 30;
            if (target->hp <= 0) {
                target->hp = 0;
                target->active = 0;
                snprintf(message, BUFFER_SIZE, "Player %ld attacked Player %ld! Player %ld is now out of HP and cannot act.\n", caster - players + 1, target - players + 1, target - players + 1);
            } else {
                snprintf(message, BUFFER_SIZE, "Player %ld attacked Player %ld! Target HP: %d\n", caster - players + 1, target - players + 1, target->hp);
            }
            break;
        
        case 2: 
            if (caster->hp >= MAX_HP) {
                snprintf(message, BUFFER_SIZE, "Player %ld has maximum HP (200). Choose another spell.\n", caster - players + 1);
                send(caster->socket, message, strlen(message), 0);
                return;
            } else {
                int heal = caster->hp / 4;
                caster->hp = caster->hp + heal > MAX_HP ? MAX_HP : caster->hp + heal;
                snprintf(message, BUFFER_SIZE, "Player %ld healed themselves! HP: %d\n", caster - players + 1, caster->hp);
            }
            break;

         case 3: 
            if (target->frozen_rounds == 0) {
                target->frozen_rounds = 2; // Set frozen rounds to 2
                caster->hp -= 20;
                if (caster->hp <= 0) {
                    caster->hp = 0;
                    caster->active = 0;
                    snprintf(message, BUFFER_SIZE, "Player %ld froze Player %ld for 2 rounds! Player %ld is now out of HP and cannot act.\n", caster - players + 1, target - players + 1, caster - players + 1);
                } else {
                    snprintf(message, BUFFER_SIZE, "Player %ld froze Player %ld for 2 rounds! Caster HP: %d\n", caster - players + 1, target - players + 1, caster->hp);
                    // Notify the target that they have been frozen
                    char frozen_message[BUFFER_SIZE];
                    snprintf(frozen_message, sizeof(frozen_message), "You have been frozen by Player %ld for 2 rounds! Your turn is skipped.\n", caster - players + 1);
                    send(target->socket, frozen_message, strlen(frozen_message), 0);
                }
            } else {
                snprintf(message, BUFFER_SIZE, "Player %ld cannot freeze an already frozen player.\n", caster - players + 1);
            }
            break;

        case 4: 
            if (caster->hp >= MAX_HP) {
                snprintf(message, BUFFER_SIZE, "Player %ld has maximum HP (200). Choose another spell.\n", caster - players + 1);
                send(caster->socket, message, strlen(message), 0);
                return;
            } else {
                int drain_amount = caster->hp / 4;
                caster->hp += drain_amount * 2 > MAX_HP ? MAX_HP : caster->hp + drain_amount * 2;
                snprintf(message, BUFFER_SIZE, "Player %ld cast spell 4 and boosted their HP! HP: %d\n", caster - players + 1, caster->hp);
            }
            break;

        default:
            snprintf(message, BUFFER_SIZE, "Invalid spell chosen.\n");
            break;
    }

    send_to_all_players(players, player_count, message);
    send_hp_status(players, player_count);
}


int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1, addrlen = sizeof(address);
    int player_count = 0, turn_index = 0;
    Player players[MAX_PLAYERS];
    char buffer[BUFFER_SIZE] = {0};

    fd_set read_fds;
    int max_sd;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].socket = -1;
        players[i].hp = MAX_HP;
        players[i].spells_cast = 0;
        players[i].frozen_rounds = 0;
        players[i].active = 1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Game lobby created. Waiting for players to join...\n");

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        max_sd = server_fd;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            int sd = players[i].socket;
            if (sd > 0) FD_SET(sd, &read_fds);
            if (sd > max_sd) max_sd = sd;
        }

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) perror("select error");

        if (FD_ISSET(server_fd, &read_fds)) {
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (new_socket < 0) {
                perror("accept");
                close(server_fd);
                exit(EXIT_FAILURE);
            }

            if (player_count >= MAX_PLAYERS) {
                send(new_socket, "Lobby is full. Try again later.\n", strlen("Lobby is full. Try again later.\n"), 0);
                close(new_socket);
            } else {
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (players[i].socket == -1) {
                        players[i].socket = new_socket;
                        player_count++;
                        printf("Player %d joined the lobby.\n", i + 1);
                        char welcome_message[BUFFER_SIZE];
                        snprintf(welcome_message, sizeof(welcome_message), "Welcome, Player %d! There are now %d players in the lobby.\n", i + 1, player_count);
                        send(new_socket, welcome_message, strlen(welcome_message), 0);
                        send_initial_instructions(&players[i], i);
                        break;
                    }
                }
            }
        }

        // Handle frozen status - NEW CODE ADDED
        if (players[turn_index].frozen_rounds > 0) {
            // Decrement frozen rounds and notify the player
            players[turn_index].frozen_rounds--;
            if (players[turn_index].frozen_rounds == 0) {
                char resume_message[BUFFER_SIZE];
                snprintf(resume_message, sizeof(resume_message), "Player %d, you can now act again!\n", turn_index + 1);
                send(players[turn_index].socket, resume_message, strlen(resume_message), 0);
            } else {
                char frozen_message[BUFFER_SIZE];
                snprintf(frozen_message, sizeof(frozen_message), "Player %d, you are frozen for %d more round(s). Your turn is skipped.\n", turn_index + 1, players[turn_index].frozen_rounds);
                send(players[turn_index].socket, frozen_message, strlen(frozen_message), 0);
            }
            turn_index = (turn_index + 1) % MAX_PLAYERS; // Move to the next player's turn
            continue; // Skip the rest of the loop
        }

        // Normal turn notification for active players
        send_turn_notification(&players[turn_index], turn_index);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            int current_socket = players[i].socket;

            if (FD_ISSET(current_socket, &read_fds)) {
                int valread = read(current_socket, buffer, BUFFER_SIZE);
                if (valread == 0) {
                    printf("Player %d has left the lobby.\n", i + 1);
                    close(current_socket);
                    players[i].socket = -1;
                    player_count--;
                    printf("Players remaining in lobby: %d\n", player_count);
                    turn_index = (turn_index + 1) % MAX_PLAYERS; // Advance turn on disconnection
                } else if (valread > 0) {
                    buffer[valread] = '\0';

                    if (strcmp(buffer, "exit") == 0) {
                        printf("Player %d left the lobby.\n", i + 1);
                        close(current_socket);
                        players[i].socket = -1;
                        player_count--;
                        printf("Players remaining in lobby: %d\n", player_count);
                    } else if (strncmp(buffer, "cast ", 5) == 0) {
                        int spell_id = 0, target_index = -1;

                        sscanf(buffer, "cast %d %d", &spell_id, &target_index);
                        spell_id = atoi(&buffer[5]);

                        if (spell_id == 1 || spell_id == 3) {
                            target_index--;

                            if (target_index < 0 || target_index >= player_count || target_index == i) {
                                send(current_socket, "Invalid target. Select a different player.\n", strlen("Invalid target. Select a different player.\n"), 0);
                                continue;
                            }
                        }

                        if (spell_id >= 1 && spell_id <= MAX_SPELLS) {
                            if (i == turn_index) {
                                if (players[i].frozen_rounds == 0) { // Ensure player isn't frozen
                                    if (spell_id == 2 || spell_id == 4) {
                                        handle_cast_spell(&players[i], &players[i], spell_id, players, player_count);
                                    } else if (target_index >= 0 && target_index < player_count && target_index != i) {
                                        handle_cast_spell(&players[i], &players[target_index], spell_id, players, player_count);
                                        turn_index = (turn_index + 1) % MAX_PLAYERS; // Advance turn after casting
                                    } else {
                                        send(current_socket, "Invalid target.\n", strlen("Invalid target.\n"), 0);
                                    }
                                } else {
                                    send(current_socket, "You are frozen and cannot cast spells this turn.\n", strlen("You are frozen and cannot cast spells this turn.\n"), 0);
                                }
                            } else {
                                send(current_socket, "It's not your turn. Please wait.\n", strlen("It's not your turn. Please wait.\n"), 0);
                            }
                        } else {
                            send(current_socket, "Invalid spell choice.\n", strlen("Invalid spell choice.\n"), 0);
                        }
                    }
                }
            }
        }
    }

    close(server_fd);
    return 0;
}
