#define _POSIX_C_SOURCE 200809L
#include "thorhex.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define HEX_START 10
#define ASCII_START 60

static struct termios orig_termios;

void die(const char *s) {
    disable_raw_mode();
    perror(s);
    exit(1);
}

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

static int get_screen_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

int editor_read_key(void) {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '3': return DEL_KEY;
                        case '1': return HOME_KEY;
                        case '4': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }
    return c;
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

    if (get_screen_size(&e->screen_rows, &e->screen_cols) == -1) {
        e->screen_rows = 24;
        e->screen_cols = 80;
    }
}

void editor_open(Editor *e, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        die("fopen");

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    if (file_size < 0)
        die("ftell");
    rewind(fp);

    e->filename = strdup(filename);
    e->size = (size_t)file_size;
    e->capacity = e->size + 1024;
    e->data = malloc(e->capacity);
    if (!e->data)
        die("malloc");

    if (file_size > 0) {
        size_t n = fread(e->data, 1, e->size, fp);
        if (n != e->size)
            die("fread");
    }
    fclose(fp);
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

void editor_free(Editor *e) {
    free(e->data);
    free(e->filename);
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
        e->pending = false; // Discard pending nibble on move
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

static void editor_draw_row(Editor *e, char *buf, size_t bufsz, size_t row_offset) {
    char *p = buf;
    char *end = buf + bufsz - 1;

    // Offset
    p += snprintf(p, end - p + 1, "%08zX  ", row_offset);

    // Hex bytes
    for (int i = 0; i < BYTES_PER_ROW; i++) {
        if (p >= end) break;
        if (row_offset + i < e->size) {
            unsigned char byte = e->data[row_offset + i];
            if (row_offset + i == e->cursor && e->hex_mode && e->pending) {
                // Show pending nibble highlighted: only first digit has the
                // entered nibble, second digit is '_'
                int digit = (e->nibble >> 4) & 0xf;
                p += snprintf(p, end - p + 1, "\x1b[7m%X\x1b[m_", digit);
            } else if (row_offset + i == e->cursor && e->hex_mode) {
                p += snprintf(p, end - p + 1, "\x1b[7m%02X\x1b[m", byte);
            } else {
                p += snprintf(p, end - p + 1, "%02X", byte);
            }
        } else {
            p += snprintf(p, end - p + 1, "  ");
        }

        if (i == 7)
            p += snprintf(p, end - p + 1, "  ");
        else if (i < BYTES_PER_ROW - 1)
            *p++ = ' ';
    }

    // Separator
    p += snprintf(p, end - p + 1, "  ");

    // ASCII
    for (int i = 0; i < BYTES_PER_ROW; i++) {
        if (p >= end) break;
        if (row_offset + i < e->size) {
            unsigned char byte = e->data[row_offset + i];
            char c = (byte >= 32 && byte < 127) ? (char)byte : '.';
            if (row_offset + i == e->cursor && !e->hex_mode)
                p += snprintf(p, end - p + 1, "\x1b[7m%c\x1b[m", c);
            else
                *p++ = c;
        } else {
            *p++ = ' ';
        }
    }

    // Clear to end of line
    p += snprintf(p, end - p + 1, "\x1b[K");
    *p = '\0';
}

static void editor_draw_status_bar(Editor *e, char *buf, size_t bufsz) {
    char *p = buf;
    char *end = buf + bufsz - 1;

    // Mode indicator
    const char *mode = e->hex_mode ? "HEX" : "ASC";
    const char *mod = e->modified ? " MODIFIED" : "";

    size_t pos = e->cursor;
    size_t total = e->size > 0 ? e->size : 0;
    size_t pct = total > 0 ? (pos * 100) / total : 0;

    int n = snprintf(p, end - p + 1, "\x1b[7m %s%s | %08zX/%08zX (%zu%%) ",
                     mode, mod, pos, total, pct);
    if (n > 0) p += n;

    // Right-aligned status message
    if (e->status_msg[0] && difftime(time(NULL), e->status_time) < 3) {
        int msg_len = strlen(e->status_msg);
        int right_col = e->screen_cols - msg_len - 1;
        if (right_col > n) {
            // Pad to right column
            int padding = right_col - (p - buf);
            if (padding > 0) {
                memset(p, ' ', padding);
                p += padding;
            }
            p += snprintf(p, end - p + 1, "%s ", e->status_msg);
        }
    }

    // Pad rest
    while (p < end && (p - buf) < e->screen_cols - 1) {
        *p++ = ' ';
    }
    p += snprintf(p, end - p + 1, "\x1b[m\x1b[K");
    *p = '\0';
}

void editor_refresh_screen(Editor *e) {
    editor_scroll(e);

    char buf[4096];
    char *p = buf;
    char *end = buf + sizeof(buf) - 1;

    // Hide cursor
    p += snprintf(p, end - p + 1, "\x1b[?25l");

    // Clear screen and go home
    p += snprintf(p, end - p + 1, "\x1b[2J\x1b[H");

    // Header
    if (e->filename)
        p += snprintf(p, end - p + 1, "\x1b[7m THORHEX  %s (%zu bytes)%c \x1b[m\x1b[K\r\n",
                      e->filename, e->size, e->modified ? '*' : ' ');
    else
        p += snprintf(p, end - p + 1, "\x1b[7m THORHEX  [No File] \x1b[m\x1b[K\r\n");

    // Content
    int rows = e->screen_rows - 3;
    if (rows <= 0) rows = 1;

    for (int r = 0; r < rows; r++) {
        size_t row_offset = e->offset + (size_t)r * BYTES_PER_ROW;
        if (row_offset <= e->size) {
            char row_buf[512];
            editor_draw_row(e, row_buf, sizeof(row_buf), row_offset);
            p += snprintf(p, end - p + 1, "%s", row_buf);
        }
        p += snprintf(p, end - p + 1, "\r\n");
    }

    // Status bar
    char status_buf[256];
    editor_draw_status_bar(e, status_buf, sizeof(status_buf));
    p += snprintf(p, end - p + 1, "%s", status_buf);

    // Position cursor
    size_t cursor_row = (e->cursor - e->offset) / BYTES_PER_ROW;
    size_t cursor_col = (e->cursor - e->offset) % BYTES_PER_ROW;

    int screen_row = (int)cursor_row + 2; // +1 for header, +1 for 1-indexed
    int screen_col;
    if (e->hex_mode) {
        screen_col = HEX_START + (int)cursor_col * 3;
        if (cursor_col >= 8) screen_col++;
        if (e->pending)
            screen_col++; // Show cursor on second digit position
    } else {
        screen_col = ASCII_START + (int)cursor_col;
    }

    if (screen_row > e->screen_rows - 2)
        screen_row = e->screen_rows - 2;
    if (screen_col > e->screen_cols)
        screen_col = e->screen_cols;

    p += snprintf(p, end - p + 1, "\x1b[%d;%dH", screen_row, screen_col);

    // Show cursor
    p += snprintf(p, end - p + 1, "\x1b[?25h");

    *p = '\0';
    write(STDOUT_FILENO, buf, p - buf);
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
        // Combine with pending nibble
        unsigned char byte = e->nibble | (unsigned char)digit;
        editor_grow_data(e, e->cursor + 1);
        if (e->cursor >= e->size)
            e->size = e->cursor + 1;
        e->data[e->cursor] = byte;
        e->modified = true;
        e->pending = false;
        // Move to next byte
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
            e->pending = false;
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
