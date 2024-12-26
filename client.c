#include "unp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>

#define MAXLINE 4096
#define BOARD_WIDTH 6
#define BOARD_HEIGHT 7
#define MAX_NAME_LEN 32
#define MAX_MSG_LEN 1024
#define MAX_CHAT_LEN 10

#define STATE_INIT 0
#define STATE_MENU 1
#define STATE_WAITING 2
#define STATE_IN_GAME 3
#define STATE_CHAT 4
#define first_line 23

#define MOVE_CURSOR(x, y)  printf("\033[%d;%dH", (x), (y))



struct chat_queue {
    char messages[10][128];  
    int front;
    int rear;
};

struct game_state {
    int state;
    // the player's info
    long long player_id; 
    char player_name[MAX_NAME_LEN];
    //opponent's info when not audience
    long long opponent_id;
    char opponent_name[MAX_NAME_LEN];
    //for audience use
    long long player_1_id;
    long long player_2_id;
    char player_1_name[MAX_NAME_LEN];
    char player_2_name[MAX_NAME_LEN];

    int player_number;
    int my_turn;
    
    int board[BOARD_WIDTH][BOARD_HEIGHT];
    int game_ended;
    struct chat_queue chat;
    int is_audience;
    int room_id;
    int vs_ai;
    int audience_count;
};

struct game_state gs;
int sockfd;
struct pollfd fds[2];
int guidestatus=0; 
int inavaildstatus=0;

void clear_screen();
void init_board();
void draw_board();
void display_menu();

void send_chat(const char* message);
void send_move(int column);

void handle_server_message(char *buf);
void handle_user_input();

void add_chat_message(const char* sender, const char* content);


void init_chat_queue(struct chat_queue *q) {
    q->front = 0;
    q->rear = 0;
}

int is_chat_queue_empty(struct chat_queue *q) {
    return q->front == q->rear;
}

int is_chat_queue_full(struct chat_queue *q) {
    return (q->rear + 1) % 10 == q->front;
}

void enqueue_chat(struct chat_queue *q, const char* sender, const char* msg) {
    if (is_chat_queue_full(q)) {
        q->front = (q->front + 1) % 10;
    }
    snprintf(q->messages[q->rear], sizeof(q->messages[q->rear]), "%s: %s", sender, msg);
    q->rear = (q->rear + 1) % 10;
}

void cleanup() {
    if (sockfd != -1) {
        close(sockfd);
    }
}

void draw_chat_box() {
    MOVE_CURSOR(first_line, 1);
    printf("+------------------------------------------------------------+");
    MOVE_CURSOR(1 + first_line, 1);
    printf("| Chat Room:                                                 |");
    MOVE_CURSOR(12 + first_line, 1);
    printf("|------------------------------------------------------------|");
    MOVE_CURSOR(14 + first_line, 1);
    printf("+------------------------------------------------------------+");
}

void draw_victory() {
    clear_screen();
    MOVE_CURSOR(0, 1);
    printf("█      █  ███   ████  █████  ███   ████  █   █\n");
    printf("█      █   █   █        █   █   █  █  █   █ █\n");
    printf(" █    █    █   █        █   █   █  ████    █\n");
    printf("  █  █     █   █        █   █   █  █   █   █\n");
    printf("   ██     ███   ████    █    ███   █   █   █  \n");
}
void draw_failure() {
    clear_screen();
    MOVE_CURSOR(0, 1);
    printf("█████    █    ███  █     █    █  ████    █████\n");
    printf("█       █ █    █   █     █    █  █  █    █\n");
    printf("████   █████   █   █     █    █  ████    █████\n");
    printf("█      █   █   █   █     █    █  █   █   █\n");
    printf("█     █     █ ███  ████   ████   █   █   █████\n");
}

void draw_draw() {
    clear_screen();
    MOVE_CURSOR(0, 1);
    printf("████   ████       █    █     █      █\n");
    printf("█   █  █  █      █ █    █   █ █    █\n");
    printf("█   █  ████     █████   █   █  █   █\n");
    printf("█   █  █   █    █   █    █ █    █ █\n");
    printf("████   █   █   █     █    █      █\n");
}

void draw_gameover() {
    clear_screen();
    MOVE_CURSOR(0, 1);
    printf(" ████     █        █     █     █████\n");
    printf("█        █ █      █ █   █ █    █\n");
    printf("█  ███  █████    █   █ █   █   █████\n");
    printf("█   █   █   █    █   █ █   █   █\n");
    printf(" ████  █     █  █     █     █  █████\n");
    printf("\n");  
    printf(" ███    █     █     █████      ████\n");
    printf("█   █    █   █      █          █  █\n");
    printf("█   █    █   █      █████      ████\n");
    printf("█   █     █ █       █          █   █\n");
    printf(" ███       █        █████      █   █\n");
}


void display_chat_history() {
    for (int i = first_line + 1; i < first_line + 12; i++) {
        MOVE_CURSOR(i, 1);
        printf("\033[K"); 
    }
    
    draw_chat_box();
    
    if (is_chat_queue_empty(&gs.chat)) {
        return;
    }
    
    int i = gs.chat.front;
    int line = first_line + 2;  
    while (i != gs.chat.rear) {
        MOVE_CURSOR(line++, 3);
        printf("\033[K%s", gs.chat.messages[i]);  
        i = (i + 1) % 10;
    }
    
    if (gs.state == STATE_IN_GAME && !gs.is_audience) {
        MOVE_CURSOR(19, 0);
        if (gs.my_turn && !gs.game_ended) {
            printf("\033[KYour turn! Enter 1-7 or ':' for chat: ");
            if(inavaildstatus){
                //
                MOVE_CURSOR(19, 0);
                printf("\033[KInvalid move! Enter 1-7 or ':' for chat: ");
            }
        } else if (!gs.game_ended) {
            printf("\033[KOpponent's turn...");
        }
    }
    else if(gs.is_audience && !gs.game_ended){
        MOVE_CURSOR(19, 0);
        printf("Press q to quit or type your message after ':'.\n");
        MOVE_CURSOR(13 + first_line,0);
    }
    MOVE_CURSOR(13 + first_line,0);
    
}


void clear_screen() {
    printf("\033[2J\033[1;1H");
}

void init_board() {
    memset(gs.board, 0, sizeof(gs.board));
}

void add_chat_message(const char* sender, const char* content) {
    enqueue_chat(&gs.chat, sender, content);
    if (gs.state == STATE_IN_GAME) {
        printf("\a");
        draw_board();
        display_chat_history();
    }
}

void init_game_state() {
    memset(&gs, 0, sizeof(gs));
    gs.state = STATE_INIT;
    init_chat_queue(&gs.chat);
}

void draw_board(){
    clear_screen();
    if (gs.is_audience) {
        MOVE_CURSOR(4,1);
        printf("\033[1m=== AUDIENCE MODE ===\033[0m\n");
    }
    
    MOVE_CURSOR(1, 1);
    printf("Room ID: %d", gs.room_id);
    
    if (gs.audience_count > 0) {
        MOVE_CURSOR(1, 20);
        printf("Audiences: %d", gs.audience_count);
        MOVE_CURSOR(13 + first_line,0);
    }
    char token = (gs.player_number == 1) ? 'O' : 'X';
    char arrow[] = "->";
    if (gs.is_audience) {
        MOVE_CURSOR(2, 4);
        printf("Player 1: %s", gs.player_1_name);
        printf(" \033[31m⬤\033[0m");
        
        MOVE_CURSOR(3, 4);
        printf("Player 2: %s", gs.player_2_name);
        printf(" \033[33m⬤\033[0m");
    } else {
        MOVE_CURSOR(2, 10);
        printf("me: %s", gs.player_name);
        printf(" %s", gs.player_number == 1 ? "\033[31m⬤\033[0m" : "\033[33m⬤\033[0m");
        
        MOVE_CURSOR(3, 4);
        printf("opponent: %s", gs.opponent_name);
        printf(" %s", gs.player_number == 1 ? "\033[33m⬤\033[0m" : "\033[31m⬤\033[0m");
    }
    MOVE_CURSOR(gs.my_turn ? 2 : 3, 1);
    printf("%s", arrow);
    MOVE_CURSOR(5, 1);
    printf("  1   2   3   4   5   6   7  \n");
    printf("-----------------------------\n");
    
    for (int i = BOARD_WIDTH - 1; i >= 0; i--) {
        printf("|");
        for (int j = 0; j < BOARD_HEIGHT; j++) {
            if (gs.board[i][j] == 1) {
                printf(" \033[31m⬤\033[0m |");
            } else if (gs.board[i][j] == 2) {
                printf(" \033[33m⬤\033[0m |");
            } else {
                printf("   |");
            }
        }
        printf("\n-----------------------------\n");
    }

    if (gs.game_ended) {
        MOVE_CURSOR(19, 0);
        printf("\nPress Enter to return to menu...\n");
    }
    
    display_chat_history();
    
    if (gs.state == STATE_IN_GAME) {
        draw_chat_box();
        display_chat_history();
    }

    if (!gs.is_audience) {
        if (gs.my_turn && !gs.game_ended) {
            MOVE_CURSOR(19, 0);
            printf("Your turn! Enter 1-7 or ':' for chat: \n");
            if(inavaildstatus){
                //inavaildstatus=0;
                MOVE_CURSOR(19, 0);
                printf("\033[KInvalid move! Enter 1-7 or ':' for chat: \n");
            }
        } else if (!gs.game_ended) {
            MOVE_CURSOR(19, 0);
            printf("Opponent's turn...\n");
        }
    }
    else if(gs.is_audience && !gs.game_ended){
        MOVE_CURSOR(19, 0);
        printf("Press q to quit or type your message after ':'.\n");
    }
    MOVE_CURSOR(13 + first_line,0);
}

void send_chat(const char* message) {
    if (strlen(message) >= MAX_MSG_LEN - 64) {
        printf("Message too long. Maximum length is %d characters.\n", MAX_MSG_LEN - 64);
        return;
    }
    char buf[MAX_MSG_LEN];
    snprintf(buf, sizeof(buf), "c%lld;%s\n", gs.player_id, message);
    Writen(sockfd, buf, strlen(buf));

}


void send_move(int column) {
    char buf[32];
    snprintf(buf, sizeof(buf), "s%lld %d\n", gs.player_id, column);
    printf("Sending: %s\n", buf);
    Writen(sockfd, buf, strlen(buf));
}

void display_status_message(const char* message) {
    MOVE_CURSOR(18, 3);
    printf("\033[K");  
    printf("%s", message);
    fflush(stdout);
}

void display_help() {
    clear_screen();
    printf("=== Connect Four Help ===\n\n");
    printf("Game Controls:\n");
    printf("  1-7     : Place piece in column\n");
    printf("  :       : Enter chat mode\n");
    printf("  /help   : Display this help\n");
    printf("  /quit   : Exit game\n\n");
    printf("Press Enter to continue...");
    getchar();
}

void display_menu() {
    clear_screen();
    printf("=== Connect Four ===\n");
    printf("Welcome, %s! (ID: %lld)\n\n", gs.player_name, gs.player_id);
    printf("1. Random Match\n");
    printf("2. Create Private Room\n");
    printf("3. Join Private Room\n");
    printf("4. Watch a Game\n");
    printf("5. Exit\n\n");
    printf("Enter your choice: ");
    fflush(stdout);
}


int get_ai_move() {

    int valid_columns[BOARD_HEIGHT];
    int valid_count = 0;
    
    for (int col = 0; col < BOARD_HEIGHT; col++) {
        if (gs.board[BOARD_WIDTH-1][col] == 0) {
            valid_columns[valid_count++] = col;
        }
    }
    if (valid_count == 0) return -1;
    return valid_columns[rand() % valid_count] + 1;
}

void handle_ai_turn() {
    if (gs.vs_ai && gs.my_turn) {
        int move = get_ai_move();
        if (move != -1) {
            send_move(move);
        }
    }
}

void handle_server_message(char *buf) {
    char *message = strtok(buf, "\n");
    while (message != NULL) {

        switch (message[0]) {
            case 'a': { 
                sscanf(message + 1, "%d", &gs.audience_count);
                if (gs.state == STATE_IN_GAME) {
                    draw_board();
                }
                break;
            }
            
            case 'r': {  
                sscanf(message + 1, "%d", &gs.room_id);
                break;
            }

            case 'i': {  
                sscanf(message + 1, "%lld", &gs.player_id);
                gs.state = STATE_MENU;
                display_menu();
                break;
            }
            case 'w': { 
                int room_id;
                if (sscanf(message + 1, "%d", &room_id) == 1) {
                    gs.room_id = room_id;  
                    MOVE_CURSOR(19, 0);
                    printf("Room created successfully! Room ID: %d\n", room_id);
                    printf("Waiting for opponent...\n");
                    gs.state = STATE_WAITING;
                }
                else if(message[1] == ' ' && strlen(message) == 2){
                    ;
                }
                else {
                    printf("%s\n", message + 1);
                }
                break;
            }

            case 'j': { 
                char name[MAX_NAME_LEN];
                sscanf(message + 1, "%s", name);
                printf("\nPlayer %s has joined the game!\n", name);
                strncpy(gs.opponent_name, name, MAX_NAME_LEN - 1);
                gs.opponent_name[MAX_NAME_LEN - 1] = '\0';
                fflush(stdout);
                break;
            }
            
            case 'c': {  
                char sender[MAX_NAME_LEN];
                char chat_msg[MAX_MSG_LEN];
                sscanf(message + 1, "%[^;];%[^\n]", sender, chat_msg);
                add_chat_message(sender, chat_msg);
                
                if (gs.state == STATE_IN_GAME) {
                    draw_board();
                    display_chat_history();
                    
                    MOVE_CURSOR(19, 0);
                    if (gs.my_turn && !gs.game_ended && !gs.is_audience) {
                        printf("Your turn! Enter 1-7 or ':' for chat: \n");
                        if(inavaildstatus){
                            //inavaildstatus=0;
                            MOVE_CURSOR(19, 0);
                            printf("\033[KInvalid move! Enter 1-7 or ':' for chat: ");
                        }
                        MOVE_CURSOR(13 + first_line,0);
                    } else if (!gs.game_ended && !gs.is_audience) {
                        printf("Opponent's turn...\n");
                        MOVE_CURSOR(13 + first_line,0);
                    }
                    else if(gs.is_audience && !gs.game_ended){
                        MOVE_CURSOR(19, 0);
                        printf("Press q to quit or type your message after ':'.\n");
                        MOVE_CURSOR(13 + first_line,0);
                    }
                    
                    fflush(stdout);
                }
                break;
            }


            case 's': { 
                int idx = 1;
                for (int i = 0; i < BOARD_WIDTH; i++) {
                    for (int j = 0; j < BOARD_HEIGHT; j++) {
                        if (message[idx]) {
                            gs.board[i][j] = message[idx] - '0';
                            idx++;
                        }
                    }
                }
                if (gs.state == STATE_IN_GAME) {
                    clear_screen();
                    draw_board();
                    MOVE_CURSOR(13 + first_line,0);
                    fflush(stdout);
                }
                break;
            }

            case 'e': {
                inavaildstatus=0;
                if (message[1] == 'T') {
                    long long timeout_player_id;
                    sscanf(message + 2, "%lld", &timeout_player_id);
                    clear_screen();
                    draw_board();
                    MOVE_CURSOR(19, 0);
                    
                    if (gs.is_audience) {
                        draw_gameover();
                        if (timeout_player_id == gs.player_1_id) {
                            printf("\nPlayer 1 (%s) lost due to timeout!\n", gs.player_1_name);
                            printf("Press Enter to return to menu...\n");
                        } else {
                            printf("\nPlayer 2 (%s) lost due to timeout!\n", gs.player_2_name);
                            printf("Press Enter to return to menu...\n");
                        }
                    } else {
                        if (timeout_player_id == gs.player_id) {
                            draw_failure();
                            printf("\nGame Over - You lost due to timeout!\n");
                            printf("Press Enter to return to menu...\n");
                        } else {
                            draw_victory();
                            printf("\nGame Over - You won! Opponent timed out!\n");
                            printf("Press Enter to return to menu...\n");
                        }
                    }
                    gs.game_ended = 1;
                    break;
                }

                if (message[1] == 'X') {
                    MOVE_CURSOR(19, 0);
                    if (gs.is_audience) {
                        printf("Game ended - A player disconnected.\n");
                    } else {
                        printf("Your opponent left.\n");
                    }
                    printf("Press Enter to return to menu...");
                    gs.game_ended = 1;
                    break;
                }

                if (message[1] == 'Q') {
                    long long quit_id;
                    sscanf(message + 2, "%lld", &quit_id);
                    MOVE_CURSOR(19, 0);
                    if (gs.is_audience) {
                        printf("Game ended - A player quit.\n");
                        printf("Press Enter to return to menu...\n");
                    }
                    else if(quit_id == gs.player_id){
                        printf("You quit the game.\n");
                        printf("Press Enter to return to menu...\n");
                    }
                    else{
                        printf("Your opponent quit.\n");
                        printf("Press Enter to return to menu...");
                    }
                    
                    gs.game_ended = 1;
                    break;
                }

                int result = message[1] - '0';
                MOVE_CURSOR(19, 0);
                if (result == 9) {
                    draw_draw();
                } else if (gs.is_audience) {
                    draw_gameover();
                    if (result == 1) {
                        printf("Player 1 (%s) wins!\n", gs.player_1_name);
                        printf("Press Enter to return to menu...\n");
                    } else {
                        printf("Player 2 (%s) wins!\n", gs.player_2_name);
                        printf("Press Enter to return to menu...\n");
                    }
                } else {
                    if (result == gs.player_number) {
                        draw_victory();
                    } else {
                        draw_failure();
                    }
                }
                fflush(stdout);
                gs.game_ended = 1;
                break;
}            
            case 'p': {
                if(message[1] == '1'){
                    sscanf(message + 2, "%s", gs.opponent_name);
                }
                else if(message[1] == '3'){
                    long long current_turn;
                    sscanf(message + 2, "%lld", &current_turn);
                    gs.my_turn = (current_turn == gs.player_id);
                }
                else if(message[1] == '4'){ 
                    sscanf(message + 2, "%d", &gs.player_number);
                }
                else if(message[1] == '2'){ 
                    sscanf(message + 2, "%lld", &gs.opponent_id);
                    gs.state = STATE_IN_GAME;
                    gs.is_audience = 0;
                    gs.game_ended = 0;  // Reset game_ended flag when starting new game
                    init_chat_queue(&gs.chat);  
                    clear_screen();
                    draw_board();
                    display_chat_history();
                    inavaildstatus=0;
                    MOVE_CURSOR(19, 0);
                    if (gs.my_turn && !gs.game_ended) {
                        printf("\033[KYour turn! Enter 1-7 or ':' for chat: ");
                        if(inavaildstatus){
                            inavaildstatus = 0;
                            MOVE_CURSOR(19, 0);
                            printf("\033[KInvalid move! Enter 1-7 or ':' for chat: ");
                        }
                        MOVE_CURSOR(13 + first_line,0);
                    } else if (!gs.game_ended) {
                        printf("\033[KOpponent's turn...");
                        MOVE_CURSOR(13 + first_line,0);
                    }
                    fflush(stdout);
                    break;
                }


                else if(message[1] == '6'){
                    if(message[2] == '1'){
                        sscanf(message + 3, "%s", gs.player_1_name);
                    }
                    else{
                        sscanf(message + 3, "%s", gs.player_2_name);
                    }
                }

                else if(message[1] == '7'){
                    if(message[2] == '1'){
                        sscanf(message + 3, "%lld", &gs.player_1_id);
                    }
                    else{
                        sscanf(message + 3, "%lld", &gs.player_2_id);
                    }
                }

                else if(message[1] == '8'){ 
                    long long current_turn;
                    sscanf(message + 2 , "%lld", &current_turn);
                    gs.my_turn = (current_turn == gs.player_1_id);
                }
                else if(message[1] == '9'){ 
                    gs.state = STATE_IN_GAME;
                    gs.is_audience = 1;
                    gs.game_ended = 0;  
                    init_chat_queue(&gs.chat);
                    clear_screen();
                    draw_board();
                    draw_chat_box();
                    display_chat_history();

                    if (gs.audience_count > 0 || gs.audience_count == 0) {
                        MOVE_CURSOR(1, 20);
                        printf("Audiences: %d", gs.audience_count);
                    }
                    fflush(stdout);
                    MOVE_CURSOR(13 + first_line,0);
                    break;
                }

            }

            default:
                printf("Unknown message from server: %s\n", message);
                break;
        }
        message = strtok(NULL, "\n");
    }
}

int is_valid_move(int column) {
    column--; 
    if (column < 0 || column >= BOARD_HEIGHT) return 0;
    
    return gs.board[BOARD_WIDTH-1][column] == 0;
}

void handle_user_input() {
    char buf[MAXLINE];
    if (fgets(buf, sizeof(buf), stdin) == NULL) return;
    buf[strcspn(buf, "\n")] = 0;

    switch (gs.state) {
        case STATE_INIT: {
            strncpy(gs.player_name, buf, sizeof(gs.player_name) - 1);
            gs.player_name[sizeof(gs.player_name) - 1] = '\0';
            char msg[64];
            snprintf(msg, sizeof(msg), "n%s\n", gs.player_name);
            Writen(sockfd, msg, strlen(msg));
            break;
        }

        case STATE_MENU: {
            int choice = atoi(buf);
            switch(choice) {
                case 1:  // Random Match
                    gs.game_ended = 0;  // Reset game_ended flag when starting new match
                    snprintf(buf, sizeof(buf), "m1%lld\n", gs.player_id);
                    Writen(sockfd, buf, strlen(buf));
                    printf("Finding a match...\n");
                    gs.state = STATE_WAITING;
                    break;
                    
                case 2:  // Create Private Room
                    gs.game_ended = 0;  // Reset game_ended flag when creating room
                    snprintf(buf, sizeof(buf), "m2%lld\n", gs.player_id);
                    Writen(sockfd, buf, strlen(buf));
                    break;
                    
                case 3: {  // Join Private Room
                    gs.game_ended = 0;  // Reset game_ended flag when joining room
                    printf("Enter room ID: ");
                    fflush(stdout);
                    if (fgets(buf, sizeof(buf), stdin)) {
                        char msg[32];
                        snprintf(msg, sizeof(msg), "m3%lld;%s", gs.player_id, buf);
                        Writen(sockfd, msg, strlen(msg));
                    }
                    break;
                }
                        
                        case 4: {  // Watch a Game
                            gs.game_ended = 0;  
                            printf("Enter room ID to watch: ");
                            fflush(stdout);
                            if (fgets(buf, sizeof(buf), stdin)) {
                                char msg[32];
                                snprintf(msg, sizeof(msg), "m4%lld;%s", gs.player_id, buf);
                                Writen(sockfd, msg, strlen(msg));
                            }
                            break;
                        }
                        
                        case 5:  // Exit
                            exit(0);
                            break;
                            
                        default:
                            printf("Invalid choice. Please enter 1-5: ");
                            fflush(stdout);
                    }
                    break;
                }

                case STATE_IN_GAME: {
                    if (gs.game_ended) {
                        if (gs.is_audience) {
                            // For audience members, send explicit leave message
                            char quit_msg[32];
                            snprintf(quit_msg, sizeof(quit_msg), "l%lld\n", gs.player_id);
                            Writen(sockfd, quit_msg, strlen(quit_msg));
                        }
                        gs.state = STATE_MENU;
                        gs.game_ended = 0;
                        init_board();
                        display_menu();
                        break;
                    }

            if (strlen(buf) == 0) {
                return;
            }

            // Handle guide command
            if (buf[0] == 'g') {
                MOVE_CURSOR(0, 0);
                guidestatus = !guidestatus;
                if (guidestatus) {
                    clear_screen();
                    printf("Guide Book\n");
                    printf("1. Enter a number from 1 to 7 to place a piece in the corresponding column.\n");
                    printf("2. Press : to start chatting.\n");
                    printf("3. Press q to exit.\n");
                    printf("4. Press g to go back to board.\n");
                } else {
                    clear_screen();
                    draw_board();
                }
                return;
            }

            // Handle quit command
            if (strcmp(buf, "q") == 0) {
                if(gs.is_audience){
                    char quit_msg[32];
                    snprintf(quit_msg, sizeof(quit_msg), "l%lld\n", gs.player_id);
                    Writen(sockfd, quit_msg, strlen(quit_msg));
                    gs.state = STATE_MENU;
                    init_board();
                    display_menu();
                    return;
                }
                char quit_msg[32];
                snprintf(quit_msg, sizeof(quit_msg), "q%lld\n", gs.player_id);
                Writen(sockfd, quit_msg, strlen(quit_msg));
                
                gs.state = STATE_MENU;
                gs.game_ended = 0;
                init_board();
                display_menu();
                return;
            }

            // Handle chat command
            if (buf[0] == ':') {
                if (strlen(buf) > 1) {  // Only send chat if there's content after ':'
                    send_chat(buf + 1);  // Send everything after the ':' character
                }
                draw_board();
                return;
            }

            // Handle move command (only when it's player's turn and not an audience)
            if (gs.my_turn && !gs.is_audience) {
                int column = atoi(buf);
                if (column >= 1 && column <= 7 && is_valid_move(column)) {
                    send_move(column);
                    inavaildstatus=0;
                } else if (column != 0) {  // Only show error if input was a number but invalid
                    MOVE_CURSOR(19, 0);
                    printf("Invalid move! Enter 1-7 or ':' for chat: ");
                    inavaildstatus=1;
                    MOVE_CURSOR(13 + first_line,0);
                    fflush(stdout);
                }
            }

            draw_board();
            MOVE_CURSOR(19, 0);
            if (gs.my_turn && !gs.game_ended && !gs.is_audience) {
                printf("Your turn! Enter 1-7 or ':' for chat: ");
                if(inavaildstatus){
                    //
                    MOVE_CURSOR(19, 0);
                    printf("\033[KInvalid move! Enter 1-7 or ':' for chat: ");
                }
                MOVE_CURSOR(13 + first_line,0);
            } else if (!gs.game_ended && !gs.is_audience) {
                printf("Opponent's turn...");
                MOVE_CURSOR(13 + first_line,0);
            }
            else if(gs.is_audience && !gs.game_ended){
                MOVE_CURSOR(19, 0);
                printf("Press q to quit or type your message after ':'.\n");
                MOVE_CURSOR(13 + first_line,0);
            }
            fflush(stdout);
            break;
        }
    }
}

int main(int argc, char **argv) {
    struct sockaddr_in servaddr;
    char recvbuf[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <ServerIP>\n", argv[0]);
        exit(1);
    }

    sockfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(12345);
    Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

    Connect(sockfd, (SA *)&servaddr, sizeof(servaddr));

    atexit(cleanup);  

    init_game_state();
    gs.state = STATE_INIT;
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    printf("Enter your name: ");
    fflush(stdout);

    while (1) {
        int nready = Poll(fds, 2, INFTIM);
        if (fds[0].revents & POLLIN) {
            handle_user_input();
        }
        if (fds[1].revents & POLLIN) {
            int n = Read(sockfd, recvbuf, MAXLINE);
            if (n <= 0) {
                printf("\nServer disconnected\n");
                exit(1);
            }
            recvbuf[n] = '\0';
            handle_server_message(recvbuf);
        }
        if(gs.state==STATE_IN_GAME){
            MOVE_CURSOR(13 + first_line,0);
        }
    }

    return 0;
}
