/* includes */

#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <unistd.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum EDITOR_KEYS {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* data */ 
/* global struct containing our editor state */ 
struct editorConfig {
    struct termios orig_termios; /* Original termios structure settings to default back to */
    int cx, cy;                  /* cursor (x, y) position */
    int screenrows;              /* Number of rows that we can show */ 
    int screencols;              /* Number of cols that we can show */ 
};

struct editorConfig E;

/* append buffer */ 
struct abuf {
    char *b;
    int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *src, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], src, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/* terminal */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    /* Set all terminal attributes back to its original termios struct 
    to revert all changes */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    /* Retrieve original terminal attributes */
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    /* modify the input flag to turn off software flow control with XOFF/XON */
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
    
    /* disable post-processing outputs for output flag */
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);

    /* Flip all bits with bitwise NOT operator and perform a bitwise AND. Has effect 
    of only flipping the relevant bits */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    /* set a timeout for read() so that it returns after certain elapsed time with no input */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; /* in 1/10ths of a second -> or 100 ms */

    /* Set the new raw mode attributes in the termios struct with the changed bit values */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'F': return END_KEY;
                    case 'H': return HOME_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

/* input */ 

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screencols -1) {
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenrows - 1) {
                E.cy++;
            }
            break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            if (c == PAGE_UP && E.cy != 0) {
                E.cy = 0;
            }
            else if (c == PAGE_DOWN && E.cy != E.screenrows - 1) {
                E.cy = E.screenrows - 1;
            }
            
            {
                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; 

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    
    //printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}

/* output */

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), 
                "Kilo editor -- version %s", KILO_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        }
        else{
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[?25l", 6); // RM - reset mode
    abAppend(&ab, "\x1b[H", 3);
    
    editorDrawRows(&ab);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // SM - set mode 

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/* init */

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();
 
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }   

    return 0;
}
