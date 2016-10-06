#ifndef NCBOX_H_
#define NCBOX_H_

#define HOR '-'
#define VER '|'
#define EDG "+"

/**
 * curses helper functions
 */
void dhline(int r, int c, int len);
void dvline(int r, int c, int len);
void drawbox(int r0, int c0, int r1, int c1);

#endif /* NCBOX_H_ */