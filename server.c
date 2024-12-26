#include "unp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <poll.h>

#define MAX_CLIENTS 50
#define MAX_ROOMS 20
#define BOARD_WIDTH 6
#define BOARD_HEIGHT 7
#define MAX_NAME_LEN 32
#define MAXLINE 4096
#define GAME_TIMEOUT 60

#define MIN_ROOM_ID 1001
#define MAX_ROOM_ID 1999
#define ROOM_SIZE (MAX_ROOM_ID - MIN_ROOM_ID + 1)
#define MAX_AUDIENCE 50 


struct Player {
    long long id;
    char name[MAX_NAME_LEN];
    int room_id;
    int fd;
    int player_number;
};

struct Room {
    int id;
    long long player_1;
    long long player_2;
    int board[BOARD_WIDTH][BOARD_HEIGHT];
    int current_turn;
    int is_active;
    time_t last_move_time;
    int is_public;
    int audience_count;
    int vs_ai;
    long long *audience;
};

struct waiting_list {
    long long players[MAX_CLIENTS];
    int count;
} waitlist;

struct Player players[MAX_CLIENTS];
struct Room rooms[MAX_ROOMS];
int room_status[ROOM_SIZE];
long long next_id = 1;

struct Player* find_player_by_id(long long id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].id == id && players[i].fd != -1) return &players[i];
    }
    return NULL;
}

struct Player* find_player_by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].fd == fd) return &players[i];
    }
    return NULL;
}

struct Room* find_room_by_id(int room_id) {
    if (room_id < MIN_ROOM_ID || room_id > MAX_ROOM_ID) return NULL;
    int idx = room_id - MIN_ROOM_ID;
    if (!room_status[idx]) return NULL;
    return &rooms[idx];
}

struct Room* find_waiting_public_room() {
    for (int i = MIN_ROOM_ID; i <= MAX_ROOM_ID; i++) {
        if (room_status[i - MIN_ROOM_ID] && 
            rooms[i - MIN_ROOM_ID].is_public && 
            rooms[i - MIN_ROOM_ID].player_2 == -1) {
            return &rooms[i - MIN_ROOM_ID];
        }
    }
    return NULL;
}

int create_room(long long player_id, int is_public);
void join_as_audience(long long player_id, int room_id);
void send_game_state_to_audience(struct Player* audience, struct Room* room);
void notify_room(int room_id, const char* message);
int join_room(long long player_id, int room_id);

void notify_room(int room_id, const char* message) {
    struct Room* room = find_room_by_id(room_id);
    if (!room) return;
    struct Player *player1 = find_player_by_id(room->player_1);
    struct Player *player2 = find_player_by_id(room->player_2);
    if (player1) Writen(player1->fd, message, strlen(message));
    if (player2) Writen(player2->fd, message, strlen(message));
    for (int i = 0; i < room->audience_count; i++) {
        struct Player* audience = find_player_by_id(room->audience[i]);
        if (audience) {
            Writen(audience->fd, message, strlen(message));
        }
    }
}

void notify_opponent(int room_id, const char* message) {
    struct Room* room = find_room_by_id(room_id);
    if (!room) return;
    struct Player *player1 = find_player_by_id(room->player_1);
    struct Player *player2 = find_player_by_id(room->player_2);
    if (player1) Writen(player1->fd, message, strlen(message));
    if (player2) Writen(player2->fd, message, strlen(message));
}

void notify_audiences(struct Room* room, const char* message) {
    if (!room) return;
    for (int i = 0; i < room->audience_count; i++) {
        struct Player* audience = find_player_by_id(room->audience[i]);
        if (audience) {
            Writen(audience->fd, message, strlen(message));
        }
    }
}

void handle_name_message(int fd, const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].fd == -1) {
            players[i].id = next_id++;
            players[i].fd = fd;
            strncpy(players[i].name, name, MAX_NAME_LEN - 1);
            players[i].name[MAX_NAME_LEN - 1] = '\0';
            players[i].room_id = -1;
            players[i].player_number = 0;
            char msg[64];
            snprintf(msg, sizeof(msg), "i%lld\n", players[i].id);
            Writen(fd, msg, strlen(msg));
            printf("Player %s connected with ID %lld\n", name, players[i].id);
            return;
        }
    }
}

int create_room(long long player_id, int is_public) {
    struct Player* player = find_player_by_id(player_id);
    if (!player) return -1;
    for (int i = MIN_ROOM_ID; i <= MAX_ROOM_ID; i++) {
        if (room_status[i - MIN_ROOM_ID] == 0) {
            int idx = i - MIN_ROOM_ID;
            rooms[idx].id = i;
            rooms[idx].player_1 = player_id;
            rooms[idx].player_2 = -1;
            rooms[idx].current_turn = player_id;
            rooms[idx].is_active = 0;
            rooms[idx].last_move_time = time(NULL);  // Initialize when room is created
            rooms[idx].is_public = is_public;
            rooms[idx].audience_count = 0;
            rooms[idx].vs_ai = 0;
            rooms[idx].audience = malloc(sizeof(long long) * MAX_AUDIENCE);
            
            memset(rooms[idx].board, 0, sizeof(rooms[idx].board));
            room_status[idx] = 1;
            player->room_id = i;
            player->player_number = 1;
            char msg[32];
            snprintf(msg, sizeof(msg), "r%d\n", i);
            Writen(player->fd, msg, strlen(msg));
            
            return i;
        }
    }
    return -1;
}

void check_game_timeouts() {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!room_status[i]) continue;
        struct Room* room = &rooms[i];
        
        // Only check active games with two players
        if (!room->is_active || room->player_2 == -1) continue;
        
        // Check if current player has exceeded timeout
        if ((current_time - room->last_move_time) > GAME_TIMEOUT) {
            // The player whose turn it is loses
            long long timeout_player_id = room->current_turn;
            
            // Send timeout notification
            char timeout_msg[32];
            snprintf(timeout_msg, sizeof(timeout_msg), "eT%lld\n", timeout_player_id);
            notify_room(room->id, timeout_msg);
            
            // Mark game as inactive
            room->is_active = 0;
            
            printf("Game in room %d ended due to timeout (Player %lld)\n", 
                   room->id, timeout_player_id);
        }
    }
}

void send_game_state_to_players(struct Room* room) {
    if (!room) return;
    
    struct Player *player1 = find_player_by_id(room->player_1);
    struct Player *player2 = find_player_by_id(room->player_2);
    if (!player1 || !player2) return;

    char msg[128];

    snprintf(msg, sizeof(msg), "a%d\n", room->audience_count);
    Writen(player1->fd, msg, strlen(msg));
    Writen(player2->fd, msg, strlen(msg));

    snprintf(msg, sizeof(msg), "r%d\n", room->id);
    Writen(player1->fd, msg, strlen(msg));
    Writen(player2->fd, msg, strlen(msg));

    room->current_turn = room->player_1;
    snprintf(msg, sizeof(msg), "p3%lld\n", room->current_turn);
    Writen(player1->fd, msg, strlen(msg));
    Writen(player2->fd, msg, strlen(msg));

    snprintf(msg, sizeof(msg), "p1%s\n", player2->name);
    Writen(player1->fd, msg, strlen(msg));
    snprintf(msg, sizeof(msg), "p41\n");
    Writen(player1->fd, msg, strlen(msg));
    snprintf(msg, sizeof(msg), "p2%lld\n", player2->id);
    Writen(player1->fd, msg, strlen(msg));
    
    snprintf(msg, sizeof(msg), "p1%s\n", player1->name);
    Writen(player2->fd, msg, strlen(msg));
    snprintf(msg, sizeof(msg), "p42\n");
    Writen(player2->fd, msg, strlen(msg));
    snprintf(msg, sizeof(msg), "p2%lld\n", player1->id);
    Writen(player2->fd, msg, strlen(msg));

    char board_msg[128] = "s";
    int idx = 1;
    for (int i = 0; i < BOARD_WIDTH; i++) {
        for (int j = 0; j < BOARD_HEIGHT; j++) {
            board_msg[idx++] = room->board[i][j] + '0';
        }
    }
    board_msg[idx++] = '\n';
    board_msg[idx] = '\0';
    
    Writen(player1->fd, board_msg, strlen(board_msg));
    Writen(player2->fd, board_msg, strlen(board_msg));
}

void init_waiting_list() {
    waitlist.count = 0;
}

void add_to_waitlist(long long player_id) {
    if (waitlist.count < MAX_CLIENTS) {
        waitlist.players[waitlist.count++] = player_id;
    }
}

long long remove_from_waitlist() {
    if (waitlist.count > 0) {
        long long player_id = waitlist.players[0];
        for (int i = 0; i < waitlist.count - 1; i++) {
            waitlist.players[i] = waitlist.players[i + 1];
        }
        waitlist.count--;
        return player_id;
        }
    return -1;
}

int join_room(long long player_id, int room_id) {
    struct Room* room = find_room_by_id(room_id);
    struct Player* player = find_player_by_id(player_id);
    if (!room || !player) return -1;

    if (room->player_2 != -1) return -1;
    if (room->player_1 == player_id) return -1;

    room->player_2 = player_id;
    player->room_id = room_id;
    player->player_number = 2;
    room->is_active = 1;
    room->last_move_time = time(NULL);  // Reset timer when second player joins

    struct Player* player1 = find_player_by_id(room->player_1);
    if (!player1) return -1;

    char msg[128];

    snprintf(msg, sizeof(msg), "r%d\n", room_id);
    Writen(player1->fd, msg, strlen(msg));
    Writen(player->fd, msg, strlen(msg));

    snprintf(msg, sizeof(msg), "j%s\n", player->name);
    Writen(player1->fd, msg, strlen(msg));
    
    send_game_state_to_players(room);

    printf("Player %lld joined room %d\n", player_id, room_id);
    return 0;
}

void handle_menu_action(long long player_id, char action, const char *param) {
    struct Player *player = find_player_by_id(player_id);
    if (!player) return;

    switch(action) {
        case '1':  
            create_room(player_id, 1);
            break;
            
        case '2':
            create_room(player_id, 0);
            break;
            
        case '3': {
            long long pid;
            int room_id;
            if (sscanf(param, "%lld;%d", &pid, &room_id) != 2) return;
            if (join_room(player_id, room_id) == -1) {
                char msg[] = "wRoom full or invalid\n";
                Writen(player->fd, msg, strlen(msg));
            }
            break;
        }
            
        case '4': {
            int room_id = atoi(param);
            join_as_audience(player_id, room_id);
            break;
        }
    }
}

int can_join_as_audience(struct Room* room, struct Player* player) {
    if (!room || !player) return 0;
    if (room->audience_count >= MAX_AUDIENCE) return 0;
    if (!room->is_active) return 0;
    
    // Check if player is already in the room (as player or audience)
    if (room->player_1 == player->id || room->player_2 == player->id) return 0;
    for (int i = 0; i < room->audience_count; i++) {
        if (room->audience[i] == player->id) return 0;
    }
    
    return 1;
}

void remove_audience_member(struct Room* room, long long player_id) {
    if (!room) return;
    
    for (int i = 0; i < room->audience_count; i++) {
        if (room->audience[i] == player_id) {
            // Shift remaining entries left
            for (int j = i; j < room->audience_count - 1; j++) {
                room->audience[j] = room->audience[j + 1];
            }
            room->audience_count--;
            
            // Update audience count for all clients in room
            char msg[32];
            snprintf(msg, sizeof(msg), "a%d\n", room->audience_count);
            notify_room(room->id, msg);
            break;
        }
    }
}

void send_game_state_to_audience(struct Player* audience, struct Room* room) {
    if (!audience || !room) return;
    
    struct Player *player1 = find_player_by_id(room->player_1);
    struct Player *player2 = find_player_by_id(room->player_2);
    
    // Only require player1 to be present to show partial state
    if (!player1) return;
    
    char msg[128];
    snprintf(msg, sizeof(msg), "r%d\n", room->id);
    Writen(audience->fd, msg, strlen(msg));

    // Send player 1 info
    snprintf(msg, sizeof(msg), "p61%s\n", player1->name);
    Writen(audience->fd, msg, strlen(msg));
    snprintf(msg, sizeof(msg), "p71%lld\n", player1->id);
    Writen(audience->fd, msg, strlen(msg));
    
    // Send player 2 info if present
    if (player2) {
        snprintf(msg, sizeof(msg), "p62%s\n", player2->name);
        Writen(audience->fd, msg, strlen(msg));
        snprintf(msg, sizeof(msg), "p72%lld\n", player2->id);
        Writen(audience->fd, msg, strlen(msg));
    }

    // Send current turn info if game is active
    if (room->is_active) {
        snprintf(msg, sizeof(msg), "p8%lld\n", room->current_turn);
        Writen(audience->fd, msg, strlen(msg));
    }

    Writen(audience->fd, "p9\n", strlen("p9\n"));

    // Send board state
    char board_msg[128] = "s";
    int idx = 1;
    for (int i = 0; i < BOARD_WIDTH; i++) {
        for (int j = 0; j < BOARD_HEIGHT; j++) {
            board_msg[idx++] = room->board[i][j] + '0';
        }
    }
    board_msg[idx++] = '\n';
    board_msg[idx] = '\0';
    Writen(audience->fd, board_msg, strlen(board_msg));
}

void cleanup_room(struct Room* room) {
    if (!room) return;
    
    // Notify all audience members that room is closing
    char msg[] = "wRoom closed\n";
    for (int i = 0; i < room->audience_count; i++) {
        struct Player* audience = find_player_by_id(room->audience[i]);
        if (audience) {
            audience->room_id = -1;
            Writen(audience->fd, msg, strlen(msg));
        }
    }
    
    if (room->audience) {
        free(room->audience);
        room->audience = NULL;
    }
    room->audience_count = 0;
    room->is_active = 0;
}

// In server.c, modify the cleanup_disconnected_client function:

void cleanup_disconnected_client(int fd) {
    struct Player* player = find_player_by_fd(fd);
    if (!player) return;

    // Remove from waitlist if present
    for (int i = 0; i < waitlist.count; i++) {
        if (waitlist.players[i] == player->id) {
            for (int j = i; j < waitlist.count - 1; j++) {
                waitlist.players[j] = waitlist.players[j + 1];
            }
            waitlist.count--;
            break;
        }
    }

    if (player->room_id != -1) {
        struct Room* room = find_room_by_id(player->room_id);
        if (room) {
            // Check if they're a player
            if (room->player_1 == player->id || room->player_2 == player->id) {
                if (room->player_1 == player->id) room->player_1 = -1;
                if (room->player_2 == player->id) room->player_2 = -1;
                
                // End game if a player disconnects
                if (room->is_active) {
                    char msg[] = "eX\n";
                    notify_room(room->id, msg);
                    room->is_active = 0;
                }
                
                // If both players are gone, cleanup room
                if (room->player_1 == -1 && room->player_2 == -1) {
                    cleanup_room(room);
                    room_status[room->id - MIN_ROOM_ID] = 0;
                }
            } else {
                // They must be an audience member
                // Send notification if game is still active
                if (room->is_active) {
                    char chat_msg[128];
                    snprintf(chat_msg, sizeof(chat_msg), "cSystem;Audience (%s) Disconnected\n", player->name);
                    notify_room(room->id, chat_msg);
                }
                remove_audience_member(room, player->id);
            }
        }
    }

    player->fd = -1;
    player->id = -1;
    player->room_id = -1;
    player->player_number = 0;
}


int check_win(int board[BOARD_WIDTH][BOARD_HEIGHT], int x, int y) {
    int player = board[x][y];
    if (player == 0) return 0;
    int count;
    
    count = 1;
    for (int i = y + 1; i < BOARD_HEIGHT && board[x][i] == player; i++) count++;
    for (int i = y - 1; i >= 0 && board[x][i] == player; i--) count++;
    if (count >= 4) return 1;
    
    count = 1;
    for (int i = x + 1; i < BOARD_WIDTH && board[i][y] == player; i++) count++;
    for (int i = x - 1; i >= 0 && board[i][y] == player; i--) count++;
    if (count >= 4) return 1;
    
    count = 1;
    for (int i = 1; x + i < BOARD_WIDTH && y + i < BOARD_HEIGHT && board[x + i][y + i] == player; i++) count++;
    for (int i = 1; x - i >= 0 && y - i >= 0 && board[x - i][y - i] == player; i++) count++;
    if (count >= 4) return 1;
    
    count = 1;
    for (int i = 1; x + i < BOARD_WIDTH && y - i >= 0 && board[x + i][y - i] == player; i++) count++;
    for (int i = 1; x - i >= 0 && y + i < BOARD_HEIGHT && board[x - i][y + i] == player; i++) count++;
    if (count >= 4) return 1;
    
    return 0;
}

void handle_chat(struct Room* room, long long sender_id, const char* message) {
    struct Player* sender = find_player_by_id(sender_id);
    if (!room || !sender || sender->room_id != room->id) return;
    
    // Validate sender is in this room (as player or audience)
    if (room->player_1 != sender_id && room->player_2 != sender_id) {
        int is_audience = 0;
        for (int i = 0; i < room->audience_count; i++) {
            if (room->audience[i] == sender_id) {
                is_audience = 1;
                break;
            }
        }
        if (!is_audience) return;
    }
    
    char chat_msg[MAX_NAME_LEN + MAXLINE];
    snprintf(chat_msg, sizeof(chat_msg), "c%s;%s\n", sender->name, message);
    
    notify_room(room->id, chat_msg);
}




void handle_move(struct Room* room, long long player_id, int column) {
    if (!room || column < 1 || column > BOARD_HEIGHT) {
        printf("Invalid move: column out of range\n");
        return;
    }
    if (!room->is_active) {
        printf("Game is not active\n");
        return;
    }
    if (room->current_turn != player_id) {
        printf("Not player's turn\n");
        return;
    }

    room->last_move_time = time(NULL);
    column--; // Convert to 0-based index
    
    int row;
    for (row = 0; row < BOARD_WIDTH; row++) {
        if (room->board[row][column] == 0) break;
    }
    if (row >= BOARD_WIDTH) return;
    
    int player_number = (player_id == room->player_1) ? 1 : 2;
    room->board[row][column] = player_number;
    
    char msg[128];
    struct Player *player1 = find_player_by_id(room->player_1);
    struct Player *player2 = find_player_by_id(room->player_2);
    if (!player1 || !player2) return;

    // Update turn
    room->current_turn = (room->current_turn == room->player_1) ? room->player_2 : room->player_1;

    // Send turn update to players and audiences
    snprintf(msg, sizeof(msg), "p3%lld\n", room->current_turn);
    Writen(player1->fd, msg, strlen(msg));
    Writen(player2->fd, msg, strlen(msg));
    
    // For audiences, use p8 format for turn updates
    snprintf(msg, sizeof(msg), "p8%lld\n", room->current_turn);
    notify_audiences(room, msg);

    // Send board update to all
    char board_msg[128] = "s";
    int idx = 1;
    for (int i = 0; i < BOARD_WIDTH; i++) {
        for (int j = 0; j < BOARD_HEIGHT; j++) {
            board_msg[idx++] = room->board[i][j] + '0';
        }
    }
    board_msg[idx++] = '\n';
    board_msg[idx] = '\0';
    
    // Send board state to everyone
    notify_room(room->id, board_msg);
    
    // Check win conditions
    if (check_win(room->board, row, column)) {
        char win_msg[8];
        snprintf(win_msg, sizeof(win_msg), "e%d\n", player_number);
        notify_room(room->id, win_msg);
        room->is_active = 0;
    } else {
        int is_full = 1;
        for (int i = 0; i < BOARD_WIDTH; i++) {
            for (int j = 0; j < BOARD_HEIGHT; j++) {
                if (room->board[i][j] == 0) {
                    is_full = 0;
                    break;
                }
            }
            if (!is_full) break;
        }
        if (is_full) {
            notify_room(room->id, "e9\n");
            room->is_active = 0;
        }
    }
}

void join_as_audience(long long player_id, int room_id) {
    struct Room* room = find_room_by_id(room_id);
    struct Player* player = find_player_by_id(player_id);
    
    if (!can_join_as_audience(room, player)) {
        if (player) {
            char msg[] = "wCannot join room as audience\n";
            Writen(player->fd, msg, strlen(msg));
        }
        return;
    }
    
    // If player was in another room, remove them first
    if (player->room_id != -1) {
        struct Room* old_room = find_room_by_id(player->room_id);
        if (old_room) {
            remove_audience_member(old_room, player_id);
        }
    }
    
    room->audience[room->audience_count++] = player_id;
    player->room_id = room_id;
    
    // Send initial game state
    if (room->player_1 != -1 && room->player_2 != -1) {
        send_game_state_to_audience(player, room);
    }
    
    // Notify everyone about updated audience count
    char msg[32];
    snprintf(msg, sizeof(msg), "a%d\n", room->audience_count);
    notify_room(room->id, msg);

    char chat_msg[128];
    snprintf(chat_msg, sizeof(chat_msg), "cSystem;New Audience Join (%s)\n", player->name);
    notify_room(room->id, chat_msg);
}

void handle_client_message(int fd, char *buf, ssize_t n) {
    if (n >= MAXLINE) {
        fprintf(stderr, "Message too long from client fd=%d\n", fd);
        return;
    }
    buf[n] = '\0';
    char *message = strtok(buf, "\n");
    while (message != NULL) {
        switch(message[0]) {
            case 'n':
                handle_name_message(fd, message + 1);
                break;

            case 'm': {
                struct Player *player = find_player_by_fd(fd);
                if (!player) break;
                
                long long player_id;
                int room_id;
                char action = message[1];
                
                switch(action) {
                    case '1': {
                        if (waitlist.count > 0) {
                            long long opponent_id = remove_from_waitlist();
                            room_id = create_room(opponent_id, 1); 
                            join_room(player->id, room_id);
                        } else {
                            add_to_waitlist(player->id);
                            char msg[] = "wMatching...\n";
                            Writen(player->fd, msg, strlen(msg));
                        }
                        break;
                    }
                    case '2': {
                        room_id = create_room(player->id, 0);
                        if (room_id != -1) {
                            char msg[32];
                            snprintf(msg, sizeof(msg), "w%d\n", room_id);
                            Writen(player->fd, msg, strlen(msg));
                            snprintf(msg, sizeof(msg), "r%d\n", room_id);
                            Writen(player->fd, msg, strlen(msg));
                        }
                        break;
                    }
                    case '3': {
                        if (sscanf(message + 2, "%lld;%d", &player_id, &room_id) == 2) {
                            if (join_room(player_id, room_id) == -1) {
                                char msg[] = "wRoom full or invalid\n";
                                Writen(player->fd, msg, strlen(msg));
                            }
                        }
                        break;
                    }
                    case '4': {
                        if (sscanf(message + 2, "%lld;%d", &player_id, &room_id) == 2) {
                            join_as_audience(player_id, room_id);
                        }
                        break;
                    }
                }
                break;
            }
            case 's': {
                long long player_id;
                int column;
                sscanf(message + 1, "%lld%d", &player_id, &column);
                struct Player* player = find_player_by_id(player_id);
                if (player && player->room_id != -1) {
                    struct Room* room = find_room_by_id(player->room_id);
                    if (room && room->current_turn == player_id) {
                        handle_move(room, player_id, column);
                    }
                }
                break;
            }
            case 'c': {
                long long sender_id;
                char chat_msg[MAXLINE];
                sscanf(message + 1, "%lld;%[^\n]", &sender_id, chat_msg);
                struct Player* sender = find_player_by_id(sender_id);
                if (sender && sender->room_id != -1) {
                    struct Room* room = find_room_by_id(sender->room_id);
                    if (room) handle_chat(room, sender_id, chat_msg);
                }
                break;
            }

            case 'q': {
                long long player_id;
                sscanf(message + 1, "%lld", &player_id);
                struct Player* player = find_player_by_id(player_id);
                if (player && player->room_id != -1) {
                    struct Room* room = find_room_by_id(player->room_id);
                    if (room) {
                        // Check if the quitting user is a player or audience
                        int is_audience = (room->player_1 != player_id && room->player_2 != player_id);
                        
                        if (is_audience) {
                            // Handle audience member quitting - just remove them and update count
                            remove_audience_member(room, player_id);
                            player->room_id = -1;
                            
                            // Update audience count for remaining users
                            char count_msg[32];
                            snprintf(count_msg, sizeof(count_msg), "a%d\n", room->audience_count);
                            notify_room(room->id, count_msg);
                        } else if (room->is_active) {
                            // Handle player quitting
                            char msg[32];
                            snprintf(msg, sizeof(msg), "eQ%lld\n", player_id);
                            notify_room(player->room_id, msg);
                            room->is_active = 0;
                            
                            if (room->player_1 == player_id) room->player_1 = -1;
                            if (room->player_2 == player_id) room->player_2 = -1;
                            
                            if (room->player_1 == -1 || room->player_2 == -1) {
                                cleanup_room(room);
                                room_status[room->id - MIN_ROOM_ID] = 0;
                            }
                            
                            player->room_id = -1;
                            player->player_number = 0;
                        }
                    }
                }
                break;
            }

            case 'l': {
                long long player_id;
                sscanf(message + 1, "%lld", &player_id);
                struct Player* player = find_player_by_id(player_id);
                if (player && player->room_id != -1) {
                    struct Room* room = find_room_by_id(player->room_id);
                    if (room) {
                        // Handle audience member leaving
                        remove_audience_member(room, player_id);
                        player->room_id = -1;
                        
                        // Just confirm menu return to the leaving player
                        char msg[] = "w ";
                        Writen(player->fd, msg, strlen(msg));

                        // Send leave notification if game is still active
                        if (room->is_active) {
                            char chat_msg[128];
                            snprintf(chat_msg, sizeof(chat_msg), "cSystem;Audience (%s) Left\n", player->name);
                            notify_room(room->id, chat_msg);
                        }
                    }
                }
                break;
            }


            default:
                printf("Unknown message from client: %s\n", message);
                break;
        }
        message = strtok(NULL, "\n");
    }
}

int main(int argc, char **argv) {
    int listenfd;
    struct sockaddr_in servaddr;
    struct pollfd clients[MAX_CLIENTS];
    int maxi = 0;

    memset(players, -1, sizeof(players));
    memset(rooms, -1, sizeof(rooms));
    memset(room_status, 0, sizeof(room_status));

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(12345);

    Bind(listenfd, (SA *)&servaddr, sizeof(servaddr));
    Listen(listenfd, LISTENQ);

    clients[0].fd = listenfd;
    clients[0].events = POLLRDNORM;
    for (int i = 1; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    printf("Server is running on port 12345...\n");

    time_t last_timeout_check = time(NULL);
    const int POLL_TIMEOUT = 1000;

    while (1) {
        int nready = Poll(clients, maxi + 1, POLL_TIMEOUT);
        time_t current_time = time(NULL);
        if (current_time - last_timeout_check >= 1) {
            check_game_timeouts();
            last_timeout_check = current_time;
        }

        if (clients[0].revents & POLLRDNORM) {
            int connfd = Accept(listenfd, NULL, NULL);
            printf("New client connected (fd=%d)\n", connfd);

            int i;
            for (i = 1; i < MAX_CLIENTS; i++) {
                if (clients[i].fd < 0) {
                    clients[i].fd = connfd;
                    clients[i].events = POLLRDNORM;
                    if (i > maxi) maxi = i;
                    break;
                }
            }
            if (--nready <= 0) continue;
        }

        for (int i = 1; i <= maxi; i++) {
            int sockfd = clients[i].fd;
            if (sockfd < 0) continue;

            if (clients[i].revents & (POLLRDNORM | POLLERR)) {
                char buf[MAXLINE];
                ssize_t n = read(sockfd, buf, MAXLINE);
                if (n <= 0) {
                    cleanup_disconnected_client(sockfd);
                    Close(sockfd);
                    clients[i].fd = -1;
                } else {
                    handle_client_message(sockfd, buf, n);
                }
                if (--nready <= 0) break;
            }
        }
    }
    return 0;
}
