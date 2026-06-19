#ifndef THORHEX_H
#define THORHEX_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <ncurses.h>

#define THORHEX_VERSION "0.1.0"
#define BYTES_PER_ROW 16

enum editor_key {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY,
};

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
    char *filename;
    bool modified;
    size_t cursor;
    size_t offset;
    bool hex_mode;
    bool pending;
    unsigned char nibble;
    int screen_rows;
    int screen_cols;
    char status_msg[80];
    time_t status_time;
    bool running;
    bool quit_confirm;
    size_t prev_cursor;
    size_t prev_offset;
    bool menu_open;
    int menu_selection;
    int welcome_selection;
    bool editor_active;
} Editor;

void editor_init(Editor *e);
bool editor_open(Editor *e, const char *filename);
bool editor_save(Editor *e);
void editor_save_as(Editor *e, const char *filename);
void editor_free(Editor *e);
void editor_move_cursor(Editor *e, int key);
void editor_process_key(Editor *e, int key);
int editor_read_key(void);
void editor_refresh_screen(Editor *e);
void enable_raw_mode(void);
void disable_raw_mode(void);
void die(const char *s);
bool editor_prompt(Editor *e, const char *prompt, char *buf, int bufsize);

#endif
