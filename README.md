# Thorhex

A minimal terminal hex editor written in C with ncurses.

## Features

- **Hex/ASCII split view** — edit in hex nibbles or ASCII directly
- **File browser** — browse and open files from within the editor
- **Color themes** — 6 themes (Blue, Green, Red, Purple, Cyan, Yellow)
- **Settings screen** — toggle default hex mode, cycle themes
- **Menu overlay** — ESC opens File menu (Open, Browse, Save, Save As, Quit)
- **Welcome screen** — quick actions when launched without a file
- **Large file support** — scrollable with Page Up/Down, Home, End

## Building

```sh
make
```

Requires `ncurses` development headers (`libncurses-dev` on Debian, `ncurses` on macOS).

## Usage

```sh
./thorhex              # welcome screen
./thorhex <file>       # open file directly
```

### Welcome Screen

| Key | Action         |
|-----|----------------|
| `1` | Open File      |
| `2` | Browse Files   |
| `3` | Create New File|
| `4` | Settings       |
| `5` | Quit           |

### Hex Editor

| Key              | Action           |
|------------------|------------------|
| Arrows           | Navigate         |
| `Tab`            | Toggle hex/ASCII |
| `0`-`9` `a`-`f`  | Hex nibble input |
| ASCII chars      | Insert in ASCII  |
| `Ctrl+S`         | Save             |
| `Ctrl+F`         | Text search      |
| `Ctrl+H`         | Hex search       |
| `Ctrl+G`         | Find next        |
| `Ctrl+Q` / `q`   | Quit             |
| `ESC`            | Open menu        |
| `PgUp` / `PgDn`  | Scroll pages     |
| `Home` / `End`   | Jump to ends     |

### File Browser

| Key         | Action               |
|-------------|----------------------|
| Arrows      | Navigate entries     |
| `Enter`     | Open file / enter dir|
| `Backspace` | Go up one directory  |
| `ESC`       | Back to welcome/menu |
| `q`         | Quit                 |

### Menu (ESC)

| Key             | Action     |
|-----------------|------------|
| Arrows / `j` `k`| Navigate   |
| `Enter`         | Select     |
| `ESC`           | Close menu |

### Settings

| Key             | Action              |
|-----------------|---------------------|
| `Enter`         | Toggle / cycle      |
| `ESC`           | Back to welcome     |

## Themes

Blue (default), Green, Red, Purple, Cyan, Yellow — selectable in Settings.

## Structure

```
src/
  main.c          Entry point
  thorhex.h       Editor struct, declarations
  thorhex.c       Editor logic, rendering, input, file browser, themes
Makefile
```

## License

MIT
