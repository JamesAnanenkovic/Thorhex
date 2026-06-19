#include "thorhex.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    Editor editor;
    editor_init(&editor);
    if (argc >= 2)
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
