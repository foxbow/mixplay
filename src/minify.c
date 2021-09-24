/*
 * one pass javascript minifier filter
 *
 * Some js statement
 * Goes on forever
 * And we're leavin'
 * Broken code behind
 *
 * Very simple javascript minifier that takes wild guesses about code
 * structures from single characters to filter out linebreaks, comments and
 * whitespaces from the original code, ending up with a single line of
 * spaghetti code.
 *
 * It works as a filter just because I am too lazy to add file handling
 * instead of using getchar() and putchar()
 *
 * The filter does take care of quoted texts and comments, so it won't fail
 * on constructs like "system.log( '/* comment in quotes *\/' )" - unlike Atom
 * for instance which needs the backslash to 'escape' the last slash..
 *
 * There may stil be some stray semicolons and most likely some constructs
 * will break the result. But it reduces the filesize of mixplayd.js
 * to 64% which is kind of worth it. CSS (~75%) and HTML (~80%) results are
 * okay'ish as expected.
 *
 * This even seems to work on CSS sheets and HTML documents.
 *
 * Next iteration (if ever) will tokenize the code and probably even shorten
 * function and variable names, which is where the fat lies.
 *
 * (c) 2021 - Björn 'foxbow' Weber
 * Use at will, no warranties
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>

/*
 * scans for the next character that is neither blank or tab to skip
 * whitespaces.
 */
static int scanChar(int next) {
	while (isblank(next)) {
		next = getchar();
	}
	return next;
}

/*
 * scans and prints a number of characters
 * used to identify <pre> and </pre> tags
 * as long as the scanned characters match the pattern, the print and print
 * will continue. If the pattern matches, the next character is returned. If
 * it it does not match, the negative if the first unmatched character is
 * returned.
 * The -1 for EOF is forwarded as is.
 */
static int scanFor(const char *pat, int next) {
	int i;

	// sanity check
	if (next == -1)
		return -1;
	for (i = 0; i < strlen(pat); i++) {
		if (tolower(next) == tolower(pat[i])) {
			putchar(next);
			next = getchar();
			if (next == -1) {
				return next;
			}
		}
		else {
			return -next;
		}
	}
	return next;
}

int main(int argc, char **argv) {
	int this, next;
	unsigned code = 0;			// does the current line need a semicolon at the end?
	unsigned mode = 0;			// 1 - //, 2 - /* */, 3 - '', 4 - "", 5 - <!--, 6 - <pre>
	unsigned html = 0;
	int assign = -1;
	unsigned blevel[10];

	this = getchar();
	if (this == '<') {
		html = 1;
	}
	while (this != -1) {
		next = getchar();
		switch (mode) {
		case 0:				// standard
			switch (this) {
			case '/':			// may start a comment
				if (!html) {
					if (next == '/') {
						mode = 1;
						this = 0;
					}
					if (next == '*') {
						mode = 2;
						this = 0;
					}
				}
				break;
			case '<':
				if (html) {
					switch (next) {
					case '!':
						next = getchar();
						if (next == '-') {
							mode = 5;
							this = 0;
						}
						else {
							putchar(this);
							putchar('!');
							this = next;
							next = getchar();
						}
						break;
					case 'p':
						putchar(this);
						next = scanFor("pre>", next);
						if (next > 0) {
							next = scanChar(next);
							if (next == '\n') {
								next = scanChar(getchar());
							}
							mode = 6;
						}
						else {
							next = -next;
						}
						this = next;
						next = scanChar(getchar());
						break;
					case 's':
						putchar(this);
						next = scanFor("script>", next);
						if (next > 0) {
							next = scanChar(next);
							if (next == '\n') {
								next = scanChar(getchar());
							}
							html = 0;
						}
						else {
							next = -next;
						}
						this = next;
						next = scanChar(getchar());
						break;
					}
				}
				else if (next == '/') {
					putchar(this);
					next = scanFor("/script>", next);
					if (next > 0) {
						next = scanChar(next);
						if (next == '\n') {
							next = scanChar(getchar());
						}
						html = 1;
					}
					else {
						next = -next;
					}
					this = next;
					next = scanChar(getchar());
					break;
				}
				else {
					next = scanChar(next);
					code = 0;
				}
				break;
			case '=':
				if (!html) {
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
				}
				/* fallthrough */
			case '&':			// no whitespaces between these and the rest
			case '*':			// no semicolon either when these are at the
			case '|':			// end of a line, most likely a broken up
			case ',':			// statement
			case '>':
			case ':':
				next = scanChar(next);
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
				next = scanChar(next);
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
				next = scanChar(next);
				break;
			case '\n':
				next = scanChar(next);
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
				next = scanChar(next);
				break;
			case '\'':
				mode = 3;
				break;
			case '"':
				mode = 4;
				break;
			case '(':
			case '{':
				next = scanChar(next);
				if (next == '\n') {
					next = getchar();
					next = scanChar(next);
				}
				if (assign > -1) {
					blevel[assign]++;
				}
				break;
			case ')':
			case '}':
				next = scanChar(next);
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
				next = scanChar(next);
				if ((next == '\n') || (next == '}')) {
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
				case '\\':
					code = 0;
					this = 0;
					break;
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
				if (next == '\n') {
					next = getchar();
				}
				else {
					putchar(this);
				}
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
				if (next == '\n') {
					next = getchar();
				}
				else {
					putchar(this);
				}
				this = next;
				next = getchar();
			}
			else if (this == '"') {
				mode = 0;
				code = 1;
			}
			break;
		case 5:
			if ((this == '-') && (next == '-')) {
				next = getchar();
				if (next == '>') {
					next = getchar();
					mode = 0;
				}
			}
			this = 0;
			break;
		case 6:
			if (this == '<') {
				putchar(this);
				next = scanFor("/pre>", next);
				if (next > 0) {
					mode = 0;
				}
				else {
					next = -next;
				}
				this = next;
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
