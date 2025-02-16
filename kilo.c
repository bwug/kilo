/*
Terminal text editor
Written by:
    Harry Hescott
Source:
    https://viewsourcecode.org/snaptoken/kilo/
Current progress:
    https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html#the-home-and-end-keys
As of:
    16th Feb 14:50
// Next step => Home and End Keys && Debug why values aren't printing on screen :3
*/

/*** includes ***/
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/*** defines && data ***/

#define CTRL_KEY(k) ((k) & 0x1f)

#define KILO_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

struct editorConfig
{
    int cx, cy;     // cursor x and y position
    int screenrows; // number of rows and columns
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

// Prints error message and exits the program using perror()
void die(const char *s) {
    perror(s);
    exit(1);
  }

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
    return;
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    // Makes a new raw terminal mode struct based on the orig_termios defined at start of code
    struct termios raw = E.orig_termios;
    
    tcgetattr(STDIN_FILENO, &raw);

    /*
        ECHO: Disables ECHOing
        ICANON: Canonical input - erase and kill processing - if exit() used it exits
        ISIG: Signal enable - SIGINT and SIGTSTP
            SIGINT: Ctrl + C => Exit the terminal
            SIGTSTP: Not sure
        IEXTEN: Ctrl + V => Pauses the terminal
        IXON: CTRL + S, CTRL + Q => software flow control
        ICRNL: How termios reads newline
        OPOST: Awkward newline handling - post-process output according to manual
        BRKINT, INPCK, ISTRIP, CS8: I was told to disable them by the guide I'm using
    */
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= (CS8);

    // Timing
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // tcsetattr sets the parameters associated with the terminal
    // TCSAFLUSH changes attr when output has drained
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
      if (nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\1xb')
    {
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1]<= '9')
            {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~')
                {
                    switch(seq[1])
                    {
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            } else {
                switch(seq[1])
                {
                    case 'A': return 'ARROW_UP';
                    case 'B': return 'ARROW_DOWN';
                    case 'C': return 'ARROW_RIGHT';
                    case 'D': return 'ARROW_LEFT';
                }
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    
    while(i < sizeof(buf) - 1)
    {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
    }
}


/*** buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** handling output ***/

void editorDrawRows(struct abuf *ab)
{
    int y;
    for(y = 0; y < E.screenrows; y++)
    {
        if(y == E.screenrows / 3)
        {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
            "Kilo editor -- version %s", KILO_VERSION);
            if(welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if(padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while(padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1)
        {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;


    abAppend(&ab, "\x1b[?25l", 6);  // Hides cursor
    abAppend(&ab, "\x1b[H", 3);     // Moves cursor to home position

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** handling input ***/
 
void editorMoveCursor(int key) {
    switch (key) {
    case ARROW_LEFT:
        if (E.cx != 0) {
          E.cx--;
        }
        break;
    case ARROW_RIGHT:
        if (E.cx != E.screencols - 1) {
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
    switch (c) {
        case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

        case PAGE_UP:
        case PAGE_DOWN:
        {
            int times = E.screenrows;
            while (times--)
            {
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        }

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

void initEditor()
{
    E.cx = 10;
    E.cy = 10;
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main()
{
    enableRawMode();
    initEditor();

    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}