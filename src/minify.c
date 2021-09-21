/*
 * one pass javascript minifier filter
 *
 * Some js statement
 * Goes on forever
 * And we're leavin'
 * Broken code behind
 *
 * Very simple javascript minifier that takes wild guesses about code
 * structures from simple characters to filter out linebreaks, comments and
 * hitespaces from the original code, ending up with a single line of
 * spaghetti code.
 *
 * It works as a filter just because I am too lazy to add file handling
 * instead of using getchar() and putchar()
 *
 * The filter does take care of quoted texts and comments, so it won't fail
 * on constructs like "system.log( '/* comment in quotes *\/' )" - unlike Atom
 * for instance which needs the backslash to 'escape' the last slash..
 *
 * There still are some stray semicolons and most likely some constructs
 * will still break the result. But it reduces the filesize of mixplayd.js
 * by 30% which is kind of worth it.
 *
 * This even seems to work on CSS sheets.
 *
 * HTML minification apparently needs some tweaking but nothing too exciting
 * it seems. But that would need command line parsing, meh...
 *
 * Next iteration (if ever) will tokenize the code and probably even shorten
 * function and variable names, which is where the fat lies.
 *
 * (c) 2021 - Björn 'foxbow' Weber
 * Use at will, no warranties
 */
#include <stdio.h>
#include <ctype.h>

int fetchChar(int next) {
	while (isblank(next)) {
		next = getchar();
	}
	return next;
}

int main(int argc, char **argv) {
	int this, next;
	unsigned code = 0;			// does the current line need a semicolon at the end?
	unsigned mode = 0;			// 1 - //, 2 - /* */, 3 - '', 4 - ""
	int assign = -1;
	unsigned blevel[10];

	this = getchar();
	while ((int) this != -1) {
		next = getchar();
		switch (mode) {
		case 0:				// standard
			switch (this) {
			case '/':			// may start a comment
				if (next == '/') {
					mode = 1;
					this = 0;
				}
				if (next == '*') {
					mode = 2;
					this = 0;
				}
				break;
			case '=':
				if (next != '=') {
					assign++;
					if (assign > 9) {
						fprintf(stderr,
								"ERROR: more than 10 assignment levels!\n");
						return 1;
					}
					blevel[assign] = 0;
				}
				else {
					/* effing '===' operator! */
					while (next == '=') {
						putchar('=');
						this = next;
						next = getchar();
					}
				}
			case '&':			// no whitespaces between this and the rest
			case '*':			// no semicolon either when these are at the
			case '|':			// end of a line, most likely a broken up
			case ',':			// statement
			case '>':
			case '<':
			case ':':
				next = fetchChar(next);
				code = 0;
				break;
			case '+':			// special case a++ will most likely need a ';'
				if (next == '+') {
					putchar('+');
					this = next;
					next = getchar();
					code = 1;
				}
				else {
					code = 0;
				}
				next = fetchChar(next);
				break;
			case '-':			// same for a--
				if (next == '-') {
					putchar('-');
					this = next;
					next = getchar();
					code = 1;
				}
				else {
					code = 0;
				}
				next = fetchChar(next);
				break;
			case '\n':
				next = fetchChar(next);
				if (assign > -1) {
					if (blevel[assign] == 0) {
						code = 1;
						assign--;
					}
				}
				if (next == '}') {
					code = 0;
				}
				if (code == 1) {
					this = ';';
					code = 0;
				}
				else {
					this = 0;
				}
				next = fetchChar(next);
				break;
			case '\'':
				mode = 3;
				break;
			case '"':
				mode = 4;
				break;
			case '(':
			case '{':
				next = fetchChar(next);
				if (next == '\n') {
					next = getchar();
					next = fetchChar(next);
				}
				if (assign > -1) {
					blevel[assign]++;
				}
				break;
			case ')':
			case '}':
				next = fetchChar(next);
				if (assign > -1) {
					blevel[assign]--;
					if ((blevel[assign] == 0) && (next != '{')) {
						assign--;
						code = 1;
					}
				}
				break;
			case ';':
				if (assign > -1) {
					if (blevel[assign] == 0) {
						assign--;
					}
				}
				next = fetchChar(next);
				if (next == '\n') {
					code = 0;
				}
				break;
			case ' ':
				switch (next) {
				case ' ':
				case '=':
				case '+':
				case '-':
				case '*':
				case '(':
				case ',':
				case '>':
				case '<':
				case '{':
				case '&':
				case '|':
					code = 0;
				case '/':
				case '}':
				case ')':
					this = 0;
					break;
				}
				break;
			default:
				code = 1;
			}
			break;
		case 1:				// line comment
			if (this == '\n') {
				mode = 0;
				if (code == 1)
					this = ';';
				else
					this = 0;
			}
			else {
				this = 0;
			}
			break;
		case 2:				// block comment
			if ((this == '*') && (next == '/')) {
				mode = 0;
				next = getchar();
			}
			this = 0;
			break;
		case 3:				// single quotes
			if (this == '\\') {
				putchar(this);
				this = next;
				next = getchar();
			}
			else if (this == '\'') {
				mode = 0;
				code = 1;
			}
			break;
		case 4:				// double quotes
			if (this == '\\') {
				putchar(this);
				this = next;
				next = getchar();
			}
			else if (this == '"') {
				mode = 0;
				code = 1;
			}
			break;
		}
		if (this) {
			putchar(this);
		}
		this = next;
	}
	return 0;
}
