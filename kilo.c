/* includes */

#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

/* data */ 
struct termios orig_termios;

/* terminal */
void die(const char *s) {
    perror(s);
    exit(1);
}

void disableRawMode() {
    /* Set all terminal attributes back to its original termios struct 
    to revert all changes */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    /* Retrieve original terminal attributes */
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = orig_termios;
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

/* init */
int main() {
    enableRawMode();

    //char c;
    //while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        }
        else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }

    return 0;
}
