#define _POSIX_C_SOURCE 200809L
#include "thorhex.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define HEX_START 10
#define ASCII_START 60

enum { MENU_NONE = -1, MENU_OPEN, MENU_SAVE, MENU_SAVE_AS, MENU_QUIT, MENU_CLOSE, MENU_COUNT };
static const char *menu_items[] = {
    "Open File", "Save", "Save As", "Quit", "Close Menu"
};
#define COL_HEADER 1
#define COL_CURSOR 2
#define COL_STATUS 3

void die(const char *s) {
    endwin();
    perror(s);
    exit(1);
}

void disable_raw_mode(void) {
    endwin();
}

void enable_raw_mode(void) {
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(50);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(COL_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(COL_CURSOR, COLOR_BLACK, COLOR_WHITE);
        init_pair(COL_STATUS, COLOR_WHITE, COLOR_BLUE);
    }
    atexit(disable_raw_mode);
}

int editor_read_key(void) {
    int ch = getch();
    switch (ch) {
        case KEY_UP:    return ARROW_UP;
        case KEY_DOWN:  return ARROW_DOWN;
        case KEY_LEFT:  return ARROW_LEFT;
        case KEY_RIGHT: return ARROW_RIGHT;
        case KEY_PPAGE: return PAGE_UP;
        case KEY_NPAGE: return PAGE_DOWN;
        case KEY_HOME:  return HOME_KEY;
        case KEY_END:   return END_KEY;
        case KEY_BACKSPACE: return BACKSPACE;
        case KEY_DC:    return DEL_KEY;
        default:        return ch;
    }
}

void editor_init(Editor *e) {
    e->data = NULL;
    e->size = 0;
    e->capacity = 0;
    e->filename = NULL;
    e->modified = false;
    e->cursor = 0;
    e->offset = 0;
    e->hex_mode = true;
    e->pending = false;
    e->nibble = 0;
    e->status_msg[0] = '\0';
    e->status_time = 0;
    e->running = true;
    e->quit_confirm = false;
    e->prev_cursor = 0;
    e->prev_offset = (size_t)-1;
    e->screen_rows = 24;
    e->screen_cols = 80;
    e->menu_open = false;
    e->menu_selection = 0;
}

bool editor_open(Editor *e, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return false;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    if (file_size < 0) { fclose(fp); return false; }
    rewind(fp);

    char *fn = strdup(filename);
    unsigned char *data = malloc((size_t)file_size + 1024);
    if (!data) { free(fn); fclose(fp); return false; }

    if (file_size > 0) {
        size_t n = fread(data, 1, (size_t)file_size, fp);
        if (n != (size_t)file_size) { free(fn); free(data); fclose(fp); return false; }
    }
    fclose(fp);

    e->filename = fn;
    e->size = (size_t)file_size;
    e->capacity = e->size + 1024;
    e->data = data;
    e->modified = false;
    e->cursor = 0;
    e->offset = 0;
    e->pending = false;
    e->prev_cursor = 0;
    e->prev_offset = (size_t)-1;
    return true;
}

bool editor_save(Editor *e) {
    if (!e->filename)
        return false;

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", e->filename);
    FILE *fp = fopen(tmp, "wb");
    if (!fp)
        return false;

    size_t n = fwrite(e->data, 1, e->size, fp);
    if (n != e->size) {
        fclose(fp);
        unlink(tmp);
        return false;
    }
    fclose(fp);

    if (rename(tmp, e->filename) == -1) {
        unlink(tmp);
        return false;
    }

    e->modified = false;
    return true;
}

void editor_set_status(Editor *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->status_msg, sizeof(e->status_msg), fmt, ap);
    va_end(ap);
    e->status_time = time(NULL);
}

static void editor_grow_data(Editor *e, size_t min_size) {
    if (min_size <= e->capacity)
        return;
    size_t new_cap = e->capacity ? e->capacity : 4096;
    while (new_cap < min_size)
        new_cap *= 2;
    unsigned char *new_data = realloc(e->data, new_cap);
    if (!new_data)
        die("realloc");
    e->data = new_data;
    e->capacity = new_cap;
}

void editor_save_as(Editor *e, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        editor_set_status(e, "Cannot open %s", filename);
        return;
    }
    size_t n = fwrite(e->data, 1, e->size, fp);
    if (n != e->size) {
        fclose(fp);
        editor_set_status(e, "Write failed");
        return;
    }
    fclose(fp);
    free(e->filename);
    e->filename = strdup(filename);
    e->modified = false;
    editor_set_status(e, "Saved as %s (%zu bytes)", filename, e->size);
}

void editor_free(Editor *e) {
    free(e->data);
    free(e->filename);
}

void editor_move_cursor(Editor *e, int key) {
    size_t cursor = e->cursor;

    switch (key) {
        case ARROW_LEFT:
            if (cursor > 0) cursor--;
            break;
        case ARROW_RIGHT:
            if (cursor < e->size) cursor++;
            break;
        case ARROW_UP:
            if (cursor >= BYTES_PER_ROW)
                cursor -= BYTES_PER_ROW;
            break;
        case ARROW_DOWN:
            if (cursor + BYTES_PER_ROW <= e->size)
                cursor += BYTES_PER_ROW;
            else if (e->size > 0)
                cursor = e->size - 1;
            break;
        case PAGE_UP: {
            int rows = e->screen_rows - 3;
            size_t step = (size_t)rows * BYTES_PER_ROW;
            cursor = cursor > step ? cursor - step : 0;
            break;
        }
        case PAGE_DOWN: {
            int rows = e->screen_rows - 3;
            size_t step = (size_t)rows * BYTES_PER_ROW;
            cursor = cursor + step > e->size ? e->size : cursor + step;
            break;
        }
        case HOME_KEY:
            cursor = (cursor / BYTES_PER_ROW) * BYTES_PER_ROW;
            break;
        case END_KEY: {
            size_t row_start = (cursor / BYTES_PER_ROW) * BYTES_PER_ROW;
            cursor = row_start + BYTES_PER_ROW - 1;
            if (cursor >= e->size && e->size > 0)
                cursor = e->size - 1;
            break;
        }
    }

    if (cursor != e->cursor) {
        e->pending = false;
        e->cursor = cursor;
    }
}

static void editor_scroll(Editor *e) {
    int rows = e->screen_rows - 3;
    if (rows <= 0) rows = 1;

    size_t cursor_row = e->cursor / BYTES_PER_ROW;
    size_t offset_row = e->offset / BYTES_PER_ROW;

    if (cursor_row < offset_row) {
        offset_row = cursor_row;
    } else if (cursor_row >= offset_row + (size_t)rows) {
        offset_row = cursor_row - (size_t)rows + 1;
    }

    e->offset = offset_row * BYTES_PER_ROW;
}

static void editor_draw_row(Editor *e, int r) {
    size_t ro = e->offset + (size_t)r * BYTES_PER_ROW;
    int y = 1 + r;

    move(y, 0);
    addch('|');
    addch(' ');

    printw("%08zX  ", ro);

    for (int i = 0; i < BYTES_PER_ROW; i++) {
        if (ro + i < e->size) {
            unsigned char byte = e->data[ro + i];
            if (ro + i == e->cursor && e->hex_mode) {
                if (e->pending) {
                    int digit = (e->nibble >> 4) & 0xf;
                    attron(COLOR_PAIR(COL_CURSOR));
                    printw("%X", digit);
                    attroff(COLOR_PAIR(COL_CURSOR));
                    addch('_');
                } else {
                    attron(COLOR_PAIR(COL_CURSOR));
                    printw("%02X", byte);
                    attroff(COLOR_PAIR(COL_CURSOR));
                }
            } else {
                printw("%02X", byte);
            }
        } else {
            printw("  ");
        }

        if (i == 7)
            printw("  ");
        else if (i < BYTES_PER_ROW - 1)
            addch(' ');
    }

    printw("  ");
    for (int i = 0; i < BYTES_PER_ROW; i++) {
        if (ro + i < e->size) {
            unsigned char byte = e->data[ro + i];
            char c = (byte >= 32 && byte < 127) ? (char)byte : '.';
            if (ro + i == e->cursor && !e->hex_mode) {
                attron(COLOR_PAIR(COL_CURSOR));
                addch(c);
                attroff(COLOR_PAIR(COL_CURSOR));
            } else {
                addch(c);
            }
        } else {
            addch(' ');
        }
    }

    int cx = getcurx(stdscr);
    while (cx < e->screen_cols - 1) {
        addch(' ');
        cx++;
    }
    addch('|');
}

bool editor_prompt(Editor *e, const char *prompt, char *buf, int bufsize) {
    int len = 0;
    buf[0] = '\0';
    while (1) {
        editor_refresh_screen(e);
        attron(COLOR_PAIR(COL_HEADER) | A_BOLD);
        mvhline(e->screen_rows - 1, 0, ' ', e->screen_cols);
        mvprintw(e->screen_rows - 1, 1, "%s%s", prompt, buf);
        move(e->screen_rows - 1, 1 + (int)strlen(prompt) + len);
        attroff(COLOR_PAIR(COL_HEADER) | A_BOLD);
        refresh();

        int c = getch();
        if (c == '\n' || c == '\r') {
            break;
        } else if (c == '\x1b') {
            buf[0] = '\0';
            return false;
        } else if ((c == 127 || c == KEY_BACKSPACE || c == '\b') && len > 0) {
            buf[--len] = '\0';
        } else if (c >= 32 && c <= 126 && len < bufsize - 1) {
            buf[len++] = c;
            buf[len] = '\0';
        } else if (c == KEY_ENTER) {
            break;
        }
    }
    e->prev_offset = (size_t)-1;
    return buf[0] != '\0';
}

static void editor_draw_menu(Editor *e) {
    int mw = 28;
    int mh = MENU_COUNT + 4;
    int mx = (e->screen_cols - mw) / 2;
    int my = (e->screen_rows - mh) / 2;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;

    // Background mask (simple — fill area)
    for (int r = 0; r < mh; r++)
        for (int c = 0; c < mw; c++)
            mvaddch(my + r, mx + c, ' ');

    // Border
    mvaddch(my, mx, '+');
    mvaddch(my, mx + mw - 1, '+');
    mvaddch(my + mh - 1, mx, '+');
    mvaddch(my + mh - 1, mx + mw - 1, '+');
    for (int c = 1; c < mw - 1; c++) {
        mvaddch(my, mx + c, '-');
        mvaddch(my + mh - 1, mx + c, '-');
    }
    for (int r = 1; r < mh - 1; r++) {
        mvaddch(my + r, mx, '|');
        mvaddch(my + r, mx + mw - 1, '|');
    }

    // Title
    attron(A_BOLD);
    mvprintw(my + 1, mx + (mw - 11) / 2, " THORHEX ");
    attroff(A_BOLD);

    // Items
    for (int i = 0; i < MENU_COUNT; i++) {
        int row = my + 3 + i;
        if (i == e->menu_selection) {
            attron(COLOR_PAIR(COL_CURSOR));
            mvprintw(row, mx + 2, "%s", menu_items[i]);
            attroff(COLOR_PAIR(COL_CURSOR));
        } else {
            mvprintw(row, mx + 2, "%s", menu_items[i]);
        }
    }
}

void editor_refresh_screen(Editor *e) {
    editor_scroll(e);
    getmaxyx(stdscr, e->screen_rows, e->screen_cols);

    int content_rows = e->screen_rows - 3;
    if (content_rows <= 0) content_rows = 1;

    bool first = (e->prev_offset == (size_t)-1);
    bool scrolled = (!first && e->offset != e->prev_offset);

    if (first || scrolled) {
        clear();

        // Header / top border:  +-- THORHEX ... --+
        attron(COLOR_PAIR(COL_HEADER) | A_BOLD);
        move(0, 0);
        addch('+');
        printw("-- THORHEX ");
        if (e->filename)
            printw("%s (%zu bytes%c) ", e->filename, e->size, e->modified ? '*' : ' ');
        else
            printw("[No File] ");
        while (getcurx(stdscr) < e->screen_cols - 1)
            addch('-');
        addch('+');
        attroff(COLOR_PAIR(COL_HEADER) | A_BOLD);

        // Content rows
        for (int r = 0; r < content_rows; r++)
            editor_draw_row(e, r);

        // Bottom border + status:  +-- HEX | ... --+
        attron(COLOR_PAIR(COL_STATUS));
        int status_row = content_rows + 1;
        move(status_row, 0);
        addch('+');
        printw("-- ");

        const char *mode = e->hex_mode ? "HEX" : "ASC";
        size_t pct = e->size > 0 ? (e->cursor * 100) / e->size : 0;
        printw("%s | %08zX/%08zX (%zu%%)  ", mode, e->cursor, e->size, pct);

        // Right-aligned status message
        if (e->status_msg[0] && difftime(time(NULL), e->status_time) < 3) {
            int msg_x = e->screen_cols - (int)strlen(e->status_msg) - 4;
            if (msg_x > getcurx(stdscr)) {
                move(status_row, msg_x);
                printw(" %s ", e->status_msg);
            }
        }

        move(status_row, getcurx(stdscr) < e->screen_cols - 1 ? getcurx(stdscr) : e->screen_cols - 2);
        while (getcurx(stdscr) < e->screen_cols - 1)
            addch('-');
        addch('+');
        attroff(COLOR_PAIR(COL_STATUS));
    } else {
        int old_r = (int)((e->prev_cursor - e->offset) / BYTES_PER_ROW);
        int new_r = (int)((e->cursor - e->offset) / BYTES_PER_ROW);

        if (old_r >= 0 && old_r < content_rows && old_r != new_r)
            editor_draw_row(e, old_r);
        if (new_r >= 0 && new_r < content_rows)
            editor_draw_row(e, new_r);

        // Update status bar (position info changed)
        int status_row = content_rows + 1;
        const char *mode = e->hex_mode ? "HEX" : "ASC";
        size_t pct = e->size > 0 ? (e->cursor * 100) / e->size : 0;
        attron(COLOR_PAIR(COL_STATUS));
        move(status_row, 4);
        printw("%s | %08zX/%08zX (%zu%%)  ", mode, e->cursor, e->size, pct);
        // Clear to right border (before the '+')
        int fill_start = getcurx(stdscr);
        move(status_row, fill_start);
        while (getcurx(stdscr) < e->screen_cols - 1)
            addch('-');
        attroff(COLOR_PAIR(COL_STATUS));
    }

    // Position cursor
    size_t cr = (e->cursor - e->offset) / BYTES_PER_ROW;
    size_t cc = (e->cursor - e->offset) % BYTES_PER_ROW;
    int sr = (int)cr + 1;
    int sc;
    if (e->hex_mode) {
        sc = HEX_START + (int)cc * 3;
        if (cc >= 8) sc++;
        if (e->pending) sc++;
    } else {
        sc = ASCII_START + (int)cc;
    }
    sc += 2; // left border '|' + space after it
    if (sr > content_rows) sr = content_rows;
    if (sc >= e->screen_cols - 1) sc = e->screen_cols - 2;

    move(sr, sc);

    if (e->menu_open)
        editor_draw_menu(e);

    refresh();

    e->prev_cursor = e->cursor;
    e->prev_offset = e->offset;
}

static void editor_handle_hex_input(Editor *e, int key) {
    int digit = -1;
    if (key >= '0' && key <= '9')
        digit = key - '0';
    else if (key >= 'a' && key <= 'f')
        digit = key - 'a' + 10;
    else if (key >= 'A' && key <= 'F')
        digit = key - 'A' + 10;

    if (digit == -1) {
        e->pending = false;
        return;
    }

    if (e->pending) {
        unsigned char byte = e->nibble | (unsigned char)digit;
        editor_grow_data(e, e->cursor + 1);
        if (e->cursor >= e->size)
            e->size = e->cursor + 1;
        e->data[e->cursor] = byte;
        e->modified = true;
        e->pending = false;
        if (e->cursor < e->size)
            e->cursor++;
    } else {
        e->nibble = (unsigned char)(digit << 4);
        e->pending = true;
    }
}

static void editor_handle_ascii_input(Editor *e, int key) {
    if (key < 32 || key > 126)
        return;
    editor_grow_data(e, e->cursor + 1);
    if (e->cursor >= e->size)
        e->size = e->cursor + 1;
    e->data[e->cursor] = (unsigned char)key;
    e->modified = true;
    if (e->cursor < e->size)
        e->cursor++;
}

static void editor_menu_action(Editor *e, int action) {
    e->menu_open = false;
    e->prev_offset = (size_t)-1;

    switch (action) {
        case MENU_OPEN: {
            char path[1024] = {0};
            if (editor_prompt(e, "Open: ", path, sizeof(path))) {
                editor_free(e);
                editor_init(e);
                if (!editor_open(e, path))
                    editor_set_status(e, "Cannot open %s", path);
            }
            break;
        }
        case MENU_SAVE:
            if (editor_save(e))
                editor_set_status(e, "Saved (%zu bytes)", e->size);
            else
                editor_set_status(e, "Save failed!");
            break;
        case MENU_SAVE_AS: {
            char path[1024] = {0};
            if (editor_prompt(e, "Save As: ", path, sizeof(path)))
                editor_save_as(e, path);
            break;
        }
        case MENU_QUIT:
            if (e->modified) {
                e->quit_confirm = true;
                editor_set_status(e, "Save changes? (y/n/c)");
            } else {
                e->running = false;
            }
            break;
        case MENU_CLOSE:
            break;
    }
}

void editor_process_key(Editor *e, int key) {
    e->status_msg[0] = '\0';

    if (e->quit_confirm) {
        if (key == 'y' || key == 'Y') {
            if (editor_save(e))
                e->running = false;
            else
                editor_set_status(e, "Save failed!");
        } else if (key == 'n' || key == 'N') {
            e->running = false;
        } else if (key == 'c' || key == 'C' || key == '\x1b') {
            editor_set_status(e, "Quit cancelled");
        }
        e->quit_confirm = false;
        return;
    }

    if (e->menu_open) {
        switch (key) {
            case ARROW_UP:
                if (e->menu_selection > 0)
                    e->menu_selection--;
                break;
            case ARROW_DOWN:
                if (e->menu_selection < MENU_COUNT - 1)
                    e->menu_selection++;
                break;
            case '\n':
            case '\r':
            case KEY_ENTER:
                editor_menu_action(e, e->menu_selection);
                break;
            case '\x1b':
                e->menu_open = false;
                e->prev_offset = (size_t)-1;
                break;
        }
        return;
    }

    switch (key) {
        case '\r':
        case '\n':
            break;

        case CTRL_KEY('q'):
        case 'q':
            if (e->modified) {
                e->quit_confirm = true;
                editor_set_status(e, "Save changes? (y/n/c)");
            } else {
                e->running = false;
            }
            break;

        case CTRL_KEY('s'):
            if (editor_save(e))
                editor_set_status(e, "Saved (%zu bytes)", e->size);
            else
                editor_set_status(e, "Save failed!");
            break;

        case '\t':
            e->hex_mode = !e->hex_mode;
            e->pending = false;
            break;

        case BACKSPACE:
        case DEL_KEY:
            e->pending = false;
            break;

        case '\x1b':
            e->menu_open = true;
            e->menu_selection = 0;
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case PAGE_UP:
        case PAGE_DOWN:
        case HOME_KEY:
        case END_KEY:
            editor_move_cursor(e, key);
            break;

        default:
            if (e->hex_mode)
                editor_handle_hex_input(e, key);
            else
                editor_handle_ascii_input(e, key);
            break;
    }
}
