#define _XOPEN_SOURCE_EXTENDED 1

#include <ncurses.h>
#include <ctype.h>
#include <locale.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <getopt.h>
#include "mPrint.h"
#include "canvas.h"
#include "tools.h"
#include "hud.h"
#include <time.h>

#define MAX_FG_COLORS 16
#define MAX_BG_COLORS 16
#define MAX_COLOR_PAIRS (MAX_FG_COLORS * MAX_BG_COLORS)
#define DEFAULT_COLOR_PAIR 1

bool is_text_mode       = false;
canvas_pixel_t **canvas = NULL;
int last_char_pressed   = -1;

int canvas_width        = -1;
int canvas_height       = -1;
int terminal_height               = -1;
int terminal_width               = -1;

struct timespec ts0;
struct timespec ts1;
//long last_cmd_ms = -1;
long last_cmd_ns = -1;



int y                   = 0;
int x                   = 0;
int quit                = 0;
int hud_color           = 7;
int current_color_pair  = 0;
int max_color_pairs     = -1;
int max_colors          = -1;
// this is the current filename
char filename[1024]     = {0};
// this is used to quickly grab info about what the current
// fg and bg color is based on the current "color pair"
// 128x128 = 16384
// 16x16 = 256
//

//int color_array[MAX_COLOR_PAIRS][2]                = { 0 };
//int color_pair_array[MAX_FG_COLORS][MAX_BG_COLORS] = { 0 };

int **color_array = NULL;
int **color_pair_array = NULL;


void init_color_arrays();
void add_block();
void delete_block();
void add_character(wchar_t c);
void add_character_and_move_right(wchar_t c);
void define_color_pairs();
void draw_hud();
void draw_initial_ascii();
void draw_canvas();
void fail_with_msg(const char *msg);
void handle_canvas_load();
void handle_input();
void handle_text_mode_input(int c);
void handle_normal_mode_input(int c);
void handle_move_right();
void handle_move_left();
void handle_move_up();
void handle_move_down();
void handle_save_inner_loop(FILE *outfile);
void handle_save();
void init_program();
void parse_arguments(int argc, char **argv);
void print_help(char **argv);
void reset_cursor();
void write_char_to_canvas(int y, int x, wchar_t c, int fg_color, int bg_color);
void cleanup();
void show_error(char *error_msg);

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);
    init_program();
    //draw_initial_ascii();
    refresh();
    while (!quit) {
        clear();
        draw_canvas();
        draw_hud();
        handle_input();
        refresh();
    }
    endwin();

    free_canvas(canvas, canvas_height);
    
    // free the color_array
    for (int i = 0; i < MAX_COLOR_PAIRS; i++) {
        free(color_array[i]);
    }
    free(color_array);

    // free the color_pair_array
    for (int i = 0; i < MAX_FG_COLORS; i++) {
        free(color_pair_array[i]);
    }
    free(color_pair_array);

    return EXIT_SUCCESS;
}

void reset_cursor() { 
    move(y, x); 
}

void print_help(char **argv) {
    printf("Usage: %s [OPTION]...\n", argv[0]);
    printf("  -f, --filename=FILENAME    specify a filename to save to\n");
    printf("  -h, --help                 display this help and exit\n");
}

void parse_arguments(int argc, char **argv) {
    mPrint("Parsing arguments...\n");
    // parsing arguments using getopt_long
    int c = -1;
    int option_index = 0;
    static struct option longoptions[] = {
        {"filename", 1, NULL, 'f'},
        {"help",     0, NULL, 'h'}
    };
    while (1) {
        c = getopt_long(argc, argv, "f:h", longoptions, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 'f':
                strncpy(filename, optarg, 1024);
                break;
            case 'h':
                print_help(argv);
                exit(EXIT_SUCCESS);
                break;
            case '?':
                if (optopt == 'f') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                }
                else if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                }
                else {
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                }
                exit(EXIT_FAILURE);
                break;
            default:
                abort();
        }
    }
}

void init_color_arrays() {
    color_array = calloc(MAX_COLOR_PAIRS, sizeof(int *));
    if (color_array == NULL) {
        mPrint("Failed to allocate memory for color_array\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_COLOR_PAIRS; i++) {
        color_array[i] = calloc(2, sizeof(int));
        if (color_array[i] == NULL) {
            mPrint("Failed to allocate memory for color_array\n");
            exit(EXIT_FAILURE);
        }
    }

    color_pair_array = calloc(MAX_FG_COLORS, sizeof(int *));
    if (color_pair_array == NULL) {
        mPrint("Failed to allocate memory for color_pair_array\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < MAX_FG_COLORS; i++) {
        color_pair_array[i] = calloc(MAX_BG_COLORS, sizeof(int));
        if (color_pair_array[i] == NULL) {
            mPrint("Failed to allocate memory for color_pair_array\n");
            exit(EXIT_FAILURE);
        }
    }
}


void define_color_pairs() {
    int current_pair = 0;
    const int local_max_fg_colors = MAX_FG_COLORS; // for now...
    const int local_max_bg_colors = MAX_BG_COLORS; // for now...
    for (int bg_color = 0; bg_color < local_max_bg_colors; bg_color++) {
        for (int fg_color = 0; fg_color < local_max_fg_colors; fg_color++) {
            init_pair(current_pair, fg_color, bg_color);
            // store the color pair in the array
            color_array[current_pair][0] = fg_color;
            color_array[current_pair][1] = bg_color;
            // store the color pair in the array
            color_pair_array[fg_color][bg_color] = current_pair;
            current_pair++;
        }
    }
    max_color_pairs = current_pair;
    max_colors = local_max_fg_colors;
}


void cleanup() {
    endwin();
    free_canvas(canvas, canvas_height);
}

void fail_with_msg(const char *msg) {
    cleanup();
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

void write_char_to_canvas(int y, int x, wchar_t c, int fg_color, int bg_color) {
    // check to make sure y,x is within the canvas
    if (y < 0 || y >= canvas_height || x < 0 || x >= canvas_width) {
        cleanup();
        fprintf(stderr, "y: %d\nx: %d\n", y, x);
        mPrint("write_char_to_canvas: y,x is out of bounds");
        exit(EXIT_FAILURE);
    }
    // make sure the other parameters arent absurd
    if (fg_color < 0 || fg_color >= max_colors) {
        cleanup();
        fprintf(stderr, "fg_color: %d\n", fg_color);
        mPrint("write_char_to_canvas: fg_color is out of bounds");
        exit(EXIT_FAILURE);
    }
    if (bg_color < 0 || bg_color >= max_colors) {
        cleanup();
        fprintf(stderr, "bg_color: %d\n", bg_color);
        mPrint("write_char_to_canvas: bg_color is out of bounds");
        exit(EXIT_FAILURE);
    }
    canvas[y][x].character = c;
    canvas[y][x].foreground_color = fg_color;
    canvas[y][x].background_color = bg_color;
} 

void draw_initial_ascii() {
#define INITIAL_ASCII_LINE_COUNT 3
    char *lines[INITIAL_ASCII_LINE_COUNT] = { 
        "Welcome to asciishade", 
        "by darkmage", 
        "www.evildojo.com" 
    };
    current_color_pair = DEFAULT_COLOR_PAIR;
    int fg_color = get_fg_color(color_array, MAX_COLOR_PAIRS, current_color_pair);
    int bg_color = get_bg_color(color_array, MAX_COLOR_PAIRS, current_color_pair);
    for (int i=0; i < 3; i++) {
        for (size_t j=0; j < strlen(lines[i]); j++) {
            write_char_to_canvas(i, j, lines[i][j], fg_color, bg_color);
        }
    }
    //canvas = read_ascii_from_filepath("test2.ascii", &canvas_height, &canvas_width);
}

void draw_canvas() {
    wchar_t c      = L' ';
    int fg_color   = 0;
    int bg_color   = 0;
    int color_pair = 0;
    int i          = -1;
    int j          = -1;
    for (i = 0; i < canvas_height; i++) {
        for (j = 0; j < canvas_width; j++) {
            // set the color pair
            // first, get the color pair
            // it should be equal to fg_color + (bg_color * 16)
            fg_color   = canvas[i][j].foreground_color;
            bg_color   = canvas[i][j].background_color;
            color_pair = color_pair_array[fg_color][bg_color];
            // draw the character
            attron(COLOR_PAIR(color_pair));
            c = canvas[i][j].character;
            // this fixes the block-rendering bug for now...
            // there has to be a better way to do this
            if ( c == L'▀' ) {
                mvaddstr(i, j, "▀");
            }           
            else if ( c == L'▄' ) {
                mvaddstr(i, j, "▄");
            }
            else if ( c == L'█' ) {
                mvaddstr(i, j, "█");
            }
            else if (c == L'░' ) {
                mvaddstr(i, j, "░");
            }
            else {
                mvaddch(i, j, c);
            }
            attroff(COLOR_PAIR(color_pair));
        }
    }
}

void add_character(wchar_t c) {
    // add the character to the canvas
    //canvas[y][x].character = c;
    // store the color component
    //canvas[y][x].foreground_color = get_fg_color(current_color_pair);
    //canvas[y][x].background_color = get_bg_color(current_color_pair);
    int fg_color = get_fg_color(color_array, MAX_COLOR_PAIRS, current_color_pair);
    int bg_color = get_bg_color(color_array, MAX_COLOR_PAIRS, current_color_pair);
    write_char_to_canvas(y, x, c, fg_color, bg_color);
}

void add_character_and_move_right(wchar_t c) {
    add_character(c);
    handle_move_right();
}

void add_block() { 
    add_character(L'█');
}

void delete_block() {
    // delete the character from the canvas
    canvas[y][x].character = L' ';
    // store the color component
    // this should be set to the default color pair
    // what was the initial ascii drawn in?
    canvas[y][x].foreground_color = 0;
    //canvas[y][x].foreground_color = get_fg_color(DEFAULT_COLOR_PAIR);
    canvas[y][x].background_color = 0;
    //canvas[y][x].background_color = get_bg_color(DEFAULT_COLOR_PAIR);
}

void incr_color_pair() { 
    current_color_pair++; 
    if (current_color_pair >= max_color_pairs) {
        current_color_pair = 0;  
    }
}

void incr_color_pair_by_max() { 
    for (int i = 0; i < max_colors; i++) {
        incr_color_pair();                                   
    }
}

void decr_color_pair() { 
    current_color_pair--; 
    if (current_color_pair < 0) { 
        current_color_pair = max_color_pairs-1; 
    }
}

void decr_color_pair_by_max() { 
    for (int i = 0; i < max_colors; i++) { 
        decr_color_pair();                                   
    }
}

// this is not an accurate function
// it is close, but we are:
//
// 1. not defining all 16 colors
// 2. not scaling up to all of the colors that ncurses OR irc can handle
// https://modern.ircdocs.horse/formatting.html
void handle_save_inner_loop(FILE *outfile) {
    if (outfile == NULL) {
        fail_with_msg("Error opening file for writing");
    }
    for (int i = 0; i < canvas_height; i++) {
        int prev_irc_fg_color = -1;
        int prev_irc_bg_color = -1;
        for (int j = 0; j < canvas_width; j++) {
            // now, instead of grabbing characters from stdscr
            // and having to do all this shit on the fly
            // we can just render from the canvas
            wchar_t wc = canvas[i][j].character;
            int foreground_color = canvas[i][j].foreground_color;
            int background_color = canvas[i][j].background_color;
            int irc_foreground_color = convert_to_irc_color(foreground_color);
            int irc_background_color = convert_to_irc_color(background_color);
            bool color_changed = prev_irc_fg_color != irc_foreground_color || prev_irc_bg_color != irc_background_color;
            //old code here for historic reasons
            //cchar_t character;
            //mvwin_wch(stdscr, i, j, &character);  // Read wide character from the canvas
            //wchar_t wc = character.chars[0];
            //attr_t attribute = character.attr;
            //int color_pair_number = PAIR_NUMBER(attribute);
            if (color_changed) 
            {
                fprintf(outfile, "\x03%02d,%02d%lc", irc_foreground_color, irc_background_color, wc);
                //fwprintf(outfile, L"\x03%02d,%02d%c", irc_foreground_color, irc_background_color, wc);
            }
            else {
                //fwprintf(outfile, L"%c", wc);
                fprintf(outfile, "%lc", wc);
            }
            prev_irc_fg_color = irc_foreground_color;
            prev_irc_bg_color = irc_background_color;
        }
        //fprintf(outfile, "\x03\n");
        fprintf(outfile, "\n");
    }   
}

void handle_save() {
    // 1. filename is empty
    if (strcmp(filename, "") == 0) { 
        // set a default filename
        strncpy(filename, "untitled.ascii", 1024);
    }
    // 2. filename is not empty
    if (strcmp(filename, "") != 0) { 
        // test writing out file
        FILE *outfile = fopen(filename, "w");
        if (outfile == NULL) {
            perror("Error opening file for writing");
            exit(-1);
        }
        handle_save_inner_loop(outfile);
        fclose(outfile);
    }
}

void handle_move_down() {
    if (y+1 < canvas_height) {
        y++;
    }
}

void handle_move_up() {
    if (y-1 >= 0) {
        y--;
    }
}

void handle_move_left() {
    if (x-1 >= 0) {
        x--;
    }
}

void handle_move_right() {
    if (x+1 < canvas_width) {
        x++;
    }
}

void handle_normal_mode_input(int c) {
    //if (c == '`') {
    if (c == 27) {
        is_text_mode = true;
    }
    else if (c=='q') {
        quit = 1;
    }
    else if (c=='c') {
        clear_canvas(canvas, canvas_height, canvas_width);
    }
    else if (c==KEY_DC) {
        // delete a block from the current location on the canvas
        delete_block();
    }
    else if (c==KEY_BACKSPACE) {
        delete_block();
        handle_move_left();
    }
    else if (c=='S')  {
        handle_save();
    }
    else if (c==KEY_DOWN) {
        handle_move_down();
    }
    else if (c==KEY_UP) {
        handle_move_up();
    }
    else if (c==KEY_LEFT) {
        handle_move_left();
    }
    else if (c==KEY_RIGHT) {
        handle_move_right();
    }
    else if (c==' ') {
        add_block();
        handle_move_right();
    }
    else if (c=='a') {
        add_block();
        handle_move_left();
    }
    else if (c=='w') {
        add_block();
        handle_move_up();
    }
    else if (c=='s') {
        add_block();
        handle_move_down();
    }
    else if (c=='d') {
        add_block();
        handle_move_right();
    }
    else if (c=='o') {
        decr_color_pair();
    }
    else if (c=='p') {
        incr_color_pair();
    }
    else if (c=='O') {
        decr_color_pair_by_max();
    }
    else if (c=='P') {
        incr_color_pair_by_max();
    }

    //numpad
    //up-left
    else if (c=='7') {
        add_block();
        handle_move_left();
        handle_move_up();
    }
    //up
    else if (c=='8') {
        add_block();
        handle_move_up();
    }
    //up-right
    else if (c=='9') {
        add_block();
        handle_move_right();
        handle_move_up();
    }  
    //left
    else if (c=='4') {
        add_block();
        handle_move_left();
    }
    //right
    else if (c=='6') {
        add_block();
        handle_move_right();
    }  
    //down-left
    else if (c=='1') {
        add_block();
        handle_move_left();
        handle_move_down();
    }  
    //down
    else if (c=='2') {
        add_block();
        handle_move_down();
    }  
    //down-right
    else if (c=='3') {
        add_block();
        handle_move_right();
        handle_move_down();
    }  
    // experimental
    else if (c=='E') {
        show_error("This is an error message");
    }
}

void handle_text_mode_input(int c) {
    //if (c == '`') {
    if (c == 27) {
        is_text_mode = false;
    }
    else if (c==KEY_DC) {
        // delete a block from the current location on the canvas
        delete_block();
    }
    else if (c==KEY_BACKSPACE) {
        delete_block();
        handle_move_left();
    }
    else if (c==KEY_LEFT) {
        handle_move_left();
    }
    else if (c==KEY_RIGHT) {
        handle_move_right();
    }
    else if (c==KEY_UP) {
        handle_move_up();
    }
    else if (c==KEY_DOWN) {
        handle_move_down();
    }
    else {
        add_character_and_move_right(c);
    }
}

void handle_input() {
    int c = getch();
    // start the clock
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    last_char_pressed = c;
    if (is_text_mode) {
        handle_text_mode_input(c);
    }
    else {
        handle_normal_mode_input(c);
    }
    // stop the clock
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    //last_cmd_ms = (ts1.tv_sec - ts0.tv_sec) * 1000 + (ts1.tv_nsec - ts0.tv_nsec) / 1000000;
    // do nanoseconds
    last_cmd_ns = (ts1.tv_sec - ts0.tv_sec) * 1000000000 + (ts1.tv_nsec - ts0.tv_nsec);
    // do microseconds
    //last_cmd_ns = last_cmd_ns / 1000;
}





void draw_hud() {
    draw_hud_background(hud_color, terminal_height, terminal_width);

    draw_hud_row_1(canvas, 
        color_array, 
        MAX_COLOR_PAIRS, 
        filename, 
        y, 
        x, 
        hud_color, 
        terminal_height, 
        terminal_width, 
        current_color_pair,
        is_text_mode
    );
    
    reset_cursor();

    draw_hud_row_2(canvas, 
            color_array, 
            MAX_COLOR_PAIRS, 
            color_pair_array, 
            MAX_FG_COLORS ,
            terminal_height, 
            terminal_width, 
            hud_color, 
            current_color_pair, 
            y, 
            x, 
            last_char_pressed
        );

    //draw_hud_row_3(terminal_height, terminal_width, hud_color, last_cmd_ms);
    draw_hud_row_3(terminal_height, terminal_width, hud_color, last_cmd_ns);
    /*
    */
    reset_cursor();
}

void init_program() {
    mPrint("Initializing program\n");
    setlocale(LC_ALL, "");
    initscr();
    clear();
    noecho();
    keypad(stdscr, true);
    start_color();
    use_default_colors();
    define_colors();
    init_color_arrays();
    define_color_pairs();
    getmaxyx(stdscr, terminal_height, terminal_width);
    // if the terminal is too small, exit
    if (terminal_width < 4) 
    {
        fprintf(stderr, "Error: terminal too small\n");
        exit(EXIT_FAILURE);
    }
    // make the cursor visible
    curs_set(1);
    // initialize the canvas
    // for now, we are going to make the canvas the same size as the terminal
    // when we go to read in ascii files,
    handle_canvas_load();
}


void handle_canvas_load() {
    int num_of_hud_rows = 3;
    canvas_height = terminal_height - num_of_hud_rows;
    canvas_width  = terminal_width;
    // at this point, if we passed a filename
    if (strcmp(filename, "")!=0 && check_if_file_exists(filename)) {
            // we will load this file into the canvas
            canvas = read_ascii_from_filepath(filename, &canvas_height, &canvas_width);
            // eventually we will have to be able to handle moving around a 
            // canvas that might be much larger than our terminal size
    }
    else {
        canvas = init_canvas(canvas_height, canvas_width);
    }
}


void show_error(char *error_msg) {
    if (error_msg != NULL) {
        clear();
        mvaddstr(0,0,error_msg);
        refresh();
        getch();
        clear();
        refresh();
    }
}


