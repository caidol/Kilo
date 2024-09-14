// ######################
// #####  Includes  #####
// ######################

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>

// #####################
// #####  Defines  #####
// #####################

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 4
#define KILO_QUIT_TIMES 3

// TODO: Finish description of this
#define CTRL_KEY(k) ((k) & 0x1f)

enum EDITOR_KEYS {
    BACKSPACE = 127,
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

/* enum for syntax highlighting types */
enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

// ##################
// #####  Data  #####
// ##################

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords; 
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
};

/* this struct defines a single line of the file that we are editing */
typedef struct erow {
    int idx;             /* Index of the row within the current file */
    int size;            /* Size of the row, excluding the null term. */
    int rsize;           /* Size of the rendered row */
    char *chars;         /* Row content */
    char *render;        /* Row content "rendered" for screen (for TABS). */
    unsigned char *hl;   /* row highlight */
    int hl_open_comment; /* Help determine if line is part of an unclosed multiline comment */
} erow;

/* global struct containing our editor state */ 
struct editorConfig {
    struct termios orig_termios; /* Original termios structure settings to default back to */
    int cx, cy;                  /* cursor (x, y) position */
    int rx;                      /* render x position */
    int screenrows;              /* Number of rows that we can show */ 
    int screencols;              /* Number of cols that we can show */ 
    int numrows;                 /* Number of rows */
    int rowoff;                  /* Offset of rows displayed */
    int coloff;                  /* Offset of cols displayed */
    erow *row;                   /* Our rows of data */
    char *filename;              /* Currently open filename */
    int dirty;                   /* File is modified but not saved */
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax; /* Current syntax highlight, or NULL */
};

struct editorConfig E; /* static instance of our editor struct to be used */

/* all filetypes supported by kilo */ 
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};

/* null terminated array of string keywords, the second type is also distinguished by 
 * terminating the string with a pipe ('|') character */ 
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",

    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", 
    "void|", NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

// ####################
// #####  Buffer  #####
// ####################

/* Append Buffer */ 
struct abuf {
    char *b;  /* Pointer to the start of the buffer in memory */
    int len;  /* The len of our buffer */
};
#define ABUF_INIT {NULL, 0}


// #################################
// #####  Function Prototypes  ##### 
// #################################

// Buffer operations
void abAppend(struct abuf *ab, const char *src, int len);
void abInsert(struct abuf *ab, const char *src, int len);
void abFree(struct abuf *ab);

// Terminal operations
void die(const char *s);
void disableRawMode();
void enableRawMode();
int editorReadKey();

// Row operations
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorUpdateRow(erow *row);
void editorInsertRow(int row_idx, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int row_idx);
void editorRowInsertChar(erow *row, int row_idx, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int row_idx);

// Editor operations
void editorInsertChar(int c, int row, int col);
void editorInsertNewline();
void editorDelChar();

// File I/O Operations
char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
void editorSave();

// Find operations
void editorFindCallback(char *query, int key);
void editorFind();

// Output 
void editorScroll();
void editorDrawRows(struct abuf *ab);
void editorShowRowNumbers(erow *row, struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);

// Input 
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorProcessKeypress();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

// Syntax highlighting
int is_separator(int c);
void editorUpdateSyntax(erow *row);
int  editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight();

// Init
void initEditor();
int main(int argc, char *argv[]);

/* 
 * This function will append the string to the buffer. This ensures we only 
 * need to make one write call and reduce the flicker effect that occurs.
 */
void abAppend(struct abuf *ab, const char *src, int len) {
    // reallocate a new block of memory the size of our current buffer length
    // as well as the len of the new message to append
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], src, len);

    // return the new character pointer to the start of the reserved storage
    // and increase the new size of the message
    ab->b = new;
    ab->len += len;
}

/*
void abInsert(struct abuf *ab, const char *src, int len, int_buf) {
    
    // We first reallocate our memory as normal
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;

    
}
*/

/* 
 * Destructor which deallocates the dynamic memory used by abuf
 */
void abFree(struct abuf *ab) {
    free(ab->b);
}

// #################################
// #####  Terminal Operations  #####
// #################################

// This function will output a descriptive error message for the 
// global errno as well as the given string before this error message
void die(const char *s) {
    // write to the stdout stream these two escape sequences
    //
    // \x1b[2J -> Erase in display (argument 2: entire screen)
    // \x1b[H  -> Move the cursor to the top left of the screen
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

// function for low-level keypress reading operations
int editorReadKey() {
    int nread;
    char c;

    // read a character
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // EAGAIN error occurs when there is no data to be read
        // often occurs when performing non-blocking I/O
        if (nread == -1 && errno != EAGAIN) die("read");
    }
  
    // check if the character starts as an escape sequence to Help
    // later identify its type
    if (c == '\x1b') {
        char seq[3];
      
        // check the next two bytes of our escape character otherwise 
        // time out the operation
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    
        // check the variant of our escape sequence and then determine what
        // form of the desired enum that we should return
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
        } else if (seq[0] == 'O') { // handle alternate variants of the HOME/END keys
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

// ############################
// #####  Row Operations  #####
// ############################

// This function will convert our char index to a "render" index
//
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    
    // Check if the occupied character space is a tab, and if 
    // that's the case, apply the number of spaces to fill up 
    // our next tab stop.
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') {
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;

    // Iterate through cx and check when a tab occurs. 
    // Then we increment the current render position until we 
    // encounter our the position specified in the parameter
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') {
            cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
        }
        cur_rx++;

        if (cur_rx > rx) return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) {
        // Count the number of encountered tabs in the row
        if (row->chars[j] == '\t') tabs++;
    }
  
    // This is more of a safety check to ensure our data structure has
    // been freed in memory before we start using it again
    free(row->render);
    
    // each occurence of a tab already accounts for 1 so we multiply by (TAB_STOP - 1)
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);
     
    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            // Here we render our tab by adding whitespace until encountering our next tab stop
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        }
        else {
            // Otherwise the value at that render idx is the same as the char idx
            row->render[idx++] = row->chars[j];
        }    
    }
    row->render[idx] = '\0'; // Dont forget to add a null byte at the end
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int row_idx, char *s, size_t len) {
    // Return if the row index is outside the range of our current boundary
    if (row_idx < 0 || row_idx > E.numrows) return;
    
    // Reallocate memory space to include a new row
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // Move all the row contents from your row idx to the final row an entire
    // row ahead. Then update all row_idx from this point until the final row
    memmove(&E.row[row_idx + 1], &E.row[row_idx], sizeof(erow) * (E.numrows - row_idx));
    for (int j = row_idx + 1; j <= E.numrows; j++) E.row[j].idx++;

    // Update the row struct for our inserted row with corresponding information
    E.row[row_idx].idx = row_idx; 
    E.row[row_idx].size = len;
    E.row[row_idx].chars = malloc(len + 1);
    memcpy(E.row[row_idx].chars, s, len); // copy our string into the chars array
    E.row[row_idx].chars[len] = '\0'; // null character to mark end of string
    
    E.row[row_idx].rsize = 0;
    E.row[row_idx].render = NULL;
    E.row[row_idx].hl = NULL;
    E.row[row_idx].hl_open_comment = 0;
    editorUpdateRow(&E.row[row_idx]);

    E.numrows++;    
    E.dirty++; // could just set to E.dirty = 1;
}

// Deallocate the row struct from the heap
// -> specifically, the render, char and highlight content
void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int row_idx) {
    if (row_idx < 0 || row_idx >= E.numrows) return;
    editorFreeRow(&E.row[row_idx]);

    // Move the memory from after the deleted row into the position of 
    // the deleted row. Then decrement all row_idx up to the final row
    memmove(&E.row[row_idx], &E.row[row_idx + 1], sizeof(erow) * (E.numrows - row_idx - 1));
    for (int j = row_idx; j < E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--; // row removed so decrement value
    E.dirty++;
}

void editorRowInsertChar(erow *row, int row_idx, int c) {
    // if out of bounds then we set the index to the final position
    if (row_idx < 0 || row_idx > row->size) row_idx = row->size;

    // Reallocate our char content to accomodate a new character
    // Add 2 to the row size to make room for the null byte
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[row_idx + 1], &row->chars[row_idx], row->size - row_idx + 1);
    row->size++; // increment for a character, note will be a size larger in memory due to null byte
    row->chars[row_idx] = c;

    // Apply row changes
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    // Reallocate memory for the new string to append
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len); // append the string to the end of te row
    
    // Update row and file state
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int row_idx) {
    if (row_idx < 0 || row_idx >= row->size) return; // boundary check 
    memmove(&row->chars[row_idx], &row->chars[row_idx + 1], row->size - row_idx);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}


// ###############################
// #####  Editor Operations  #####
// ###############################

// Default arguments for row and col are NULL
void editorInsertChar(int c, int row, int col) {
    // if cursor is on tilde after the end of the file, then well
    // add a new row object to insert our characters into
    if (E.cy == E.numrows) {
        editorInsertRow(E.cy, "", 0);
    }

    editorRowInsertChar(row != -1 ? &E.row[row] : &E.row[E.cy], 
        col != -1 ? col : E.cx, c);
    E.cx++; // incrememnt our new position as we add a character in
}

void editorInsertNewline() {
    // Insert blank row if we're at the beginning of the line. This is 
    // because all the row content will be put onto the next line
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else { // Otherwise, we split the line we're on into two rows 
        erow *row = &E.row[E.cy];
        
        // Insert the row content after the current x position onto 
        // the next line.
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        
        // set the current row with the new content, change its size, 
        // add null byte and update etc

        row = &E.row[E.cy]; // we reassign this pointer because editorInsertRow() calls realloc()
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    // set cursor to the beginning of the next row
    E.cy++;
    E.cx = 0;
}

void editorDelChar() {
    // Return if the cursor is past the end of the file or there is no 
    // content to be deleted
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        // Remove the current char and move our x-position to 1 before
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        // Else we move our x-position to the final position of our 
        // previous line, then append our current row to the row before
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

// #################################
// #####  File I/O Operations  #####
// ################################# 

// This function enables disk-saving operations
char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    
    // Find the total length of all the content. We add 1 to each
    // row to include each null byte
    for (j = 0; j < E.numrows; j++) {
        totlen += E.row[j].size + 1;
    }
    *buflen = totlen; // assign the pointer with the length to be used later
    
    // Allocate the memory and assign a position pointer to the start of this memory buffer. 
    // Iterate through and copy each row into this buffer
    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();
    
    // Open the file using our filename
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");
    
    // Set a line pointer to help allocate new memory for every new line
    // that is read, and linecap to know how much to allocate
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    // Check if we are still able to read a line, and if so then we further
    // find the character length by stripping off for every newline or 
    // carriage return (\n and \r). Then, we will finally insert the new row
    // data into the editor.
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' 
                              || line[linelen - 1] == '\r'))
            linelen--; 
        editorInsertRow(E.numrows, line, linelen);
    }
    // free line pointer and close file, then set dirty to 0 as we haven't edited 
    // any content yet
    free(line);
    fclose(fp);
    E.dirty = 0; // reset dirty flag
}

void editorSave() {
    // Check if the filename is NULL, which occurs when we open our editor without 
    // passing in an argument
    if (E.filename == NULL) {
        // Get our new filename via editor prompt and abort if still NULL, otherwise
        // apply language specific highlighting
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }
  
    // Get our length and buffer content
    int len;
    char *buf = editorRowsToString(&len);

    // Create a new file if it doesn't yet exist already
    // 
    //    O_RDWR  -> open file for reading and writing purposes
    //    O_CREAT -> create a new file if it doesn't exist already
    //    0644    -> maps to standard file perms
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644); 
    
    /* error handling */
    if (fd != -1) {
        // ftruncate sets the file size to the specified length
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) { // expect write() function to return the number of bytes given to write
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    // We try to add a layer of security above by checking if the write call succeeded
    // It's possible to pass in the O_TRUNC flag to open, truncating the file completely
    // However, this makes the file completely empty before writing our new data into it
    // If write() were to fail, we could lose all our data. Modern editors will create a
    // temporary file and then rename it, with way more error checks.
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

// #############################
// #####  Find Operations  #####
// #############################

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;

    if (saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }
    
    if (last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for (i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;
    
        erow *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;
         
    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", 
                               editorFindCallback);
    if (query) {
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    } 
}

// ####################
// #####  Output  #####
// ####################

void editorScroll() {
    E.rx = E.cx;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
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
            } else{
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;

            char *c = &E.row[filerow].render[E.coloff];
            
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1;

            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        abAppend(ab, buf, clen);
                    }
                }else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);

                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorShiftRowContent(erow *row, int shift_amount) {
    if (shift_amount <= 0) return;

    // Allocate new space for the row content 
    char *new_chars = realloc(row->chars, row->size + shift_amount);
    if (new_chars == NULL) return;

    memmove(new_chars + shift_amount, row->chars, row->size);
    memset(new_chars, ' ', shift_amount);
    
    row->chars = new_chars;
    row->size += shift_amount;
}

void editorShowRowNumbers(erow *row, struct abuf *ab) {
    // Start by pointing to the initial memory location of our char
    // content
    char *p = ab->b;
    char buf[32]; 
  
    int curr_row = 0;
    int filerow;

    // Go through the pointer until we reach the end of the memory segment
    while (p < (ab->b + ab->len) && curr_row < curr_row + E.screenrows - 2) {
        // TODO: work out the line number at the top of the curr buffer
        filerow = curr_row + E.rowoff;    

        if (curr_row >= E.numrows) break;
        if (p + E.row[curr_row].size > ab->b + ab->len) break;
        
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", curr_row + 1, 0);
        abAppend(ab, buf, strlen(buf));
        
        if (filerow == E.cy) {
            // Highlight current rows red
            abAppend(ab, "\x1b[4;31m", strlen("\x1b[4;31m")); 
        } else {      
            // Normal rows highlight green
            abAppend(ab, "\x1b[4;32m", strlen("\x1b[4;32m")); 
        }
        
        char row_buf[16];
        snprintf(row_buf, sizeof(row_buf), "%d", filerow + 1);
        abAppend(ab, row_buf, strlen(row_buf));
        abAppend(ab, "\x1b[0m", 4); // Reset colours back to default

        p += E.row[curr_row].size; // TODO: might need to change to rsize
        curr_row++;
    }
    abAppend(ab, "\x1b[999C\x1b[999B", 12);
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
        E.filename ? E.filename : "[No name]", E.numrows,
        E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", 
      E.syntax ? E.syntax->filetype : "no filetype", E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }    
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5){
        abAppend(ab, E.statusmsg, msglen);
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[?25l", 6); // RM - reset mode
    abAppend(&ab, "\x1b[H", 3);
    
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    editorShowRowNumbers(&E.row[0], &ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // SM - set mode 

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

// ###################
// #####  Input  #####
// ###################

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            editorSetStatusMessage("");

            if (callback) callback(buf, c);

            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");

                if (callback) callback(buf, c);

                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf, c);
    }
}

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch(c) {
        case '\r':
            editorInsertNewline(); 
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING: File has unsaved changes. "
                  "Press CTRL-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
 
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows) {
                E.cx = E.row[E.cy].size;
            }
            break;
        
        case CTRL_KEY('f'):
            editorFind();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN: 
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }
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

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            editorInsertChar(c, -1, -1);
            break;
    }
    quit_times = KILO_QUIT_TIMES;
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

// #################################
// #####  Syntax Highlighting  #####
// #################################

int is_separator(int c) {
    const char *seps = ",.()+-/*=~%<>[];"; // list of separators
    return isspace(c) || c == '\0' || strchr(seps, c) != NULL;
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL) return;
    
    char **keywords = E.syntax->keywords;

    /* scs means singleline_comment_start */
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
        
        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }
 
        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = (keywords[j][klen - 1] == '|'); // check terminating char 
                if (kw2) klen--;
 
                if (!strncmp(&row->render[i], keywords[j], klen) && 
                    is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }
    
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numrows) {
        editorUpdateSyntax(&E.row[row->idx + 1]);
    }
}

int editorSyntaxToColor(int hl) {
    switch(hl) {
        case HL_COMMENT: 
        case HL_MLCOMMENT:  return 36;   /* cyan */
        case HL_KEYWORD1:   return 33;   /* yellow */
        case HL_KEYWORD2:   return 31;   /* red */
        case HL_STRING:     return 32;   /* green */
        case HL_NUMBER:     return 34;   /* blue */
        case HL_MATCH:      return 35;   /* magenta */
        default: return 37;
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    char *ext = strrchr(E.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }

                return;
            }
            i++;
        }
    }
}

// ##################
// #####  Init  #####
// ##################

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.dirty = 0;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    
    editorSetStatusMessage(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }   

    return 0;
}
