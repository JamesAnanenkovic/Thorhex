#include "thorhex.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: thorhex <file>\n");
        return 1;
    }

    Editor editor;
    editor_init(&editor);
    editor_open(&editor, argv[1]);
    enable_raw_mode();

    while (editor.running) {
        editor_refresh_screen(&editor);
        int key = editor_read_key();
        editor_process_key(&editor, key);
    }

    disable_raw_mode();
    editor_free(&editor);
    return 0;
}
