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
 * (c) 2021-2022 - Björn 'foxbow' Weber
 * Use at will, no warranties
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

/*
 * scans for the next character that is neither blank or tab to skip
 * whitespaces.
 */
static int32_t scanChar(int32_t next) {
	while ((next >= 0) && isblank(next)) {
		next = getchar();
	}
	return next;
}

/*
 * scans and prints a number of characters
 * used to identify <pre> and </pre> tags
 * as long as the scanned characters match the pattern, they are printed and 
 * the scan will continue. 
 * If the pattern matches, the next character is returned. 
 * If it it does not match, the negative of the first unmatched character is
 * returned.
 * The -1 for EOF is forwarded as is.
 */
static int32_t scanFor(const char *pat, int32_t next) {
	// sanity check
	if (next == -1)
		return -1;

	for (unsigned i = 0; i < strlen(pat); i++) {
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

int32_t main(int32_t argc, char **argv) {
	int32_t current, next;
	uint32_t code = 0;			// does the current line need a semicolon at the end?
	uint32_t mode = 0;			// 0 - txt, 1 - //, 2 - /* */, 3 - '', 4 - "", 5 - <!--, 6 - <pre>
	uint32_t html = 0;
	int32_t assign = -1;
	uint32_t blevel[10];

	current = getchar();
	// The very first character is a '<'? So it must be HTML!
	if (current == '<') {
		html = 1;
	}

	while (current != -1) {
		next = getchar();		// get lookahead
		switch (mode) {
		case 0:				// standard
			switch (current) {
			case '/':			// may start a comment
				if (!html) {
					if (next == '/') {
						mode = 1;
						current = 0;
					}
					if (next == '*') {
						mode = 2;
						current = 0;
					}
					next = scanChar(next);
				}
				break;
			case '<':
				if (html) {
					/* yes, we're really couragous enough to parse HTML! */
					switch (next) {
					case '!':
						next = getchar();
						if (next == '-') {
							mode = 5;
							current = 0;
						}
						else {
							putchar(current);
							putchar('!');
							current = next;
							next = getchar();
						}
						break;
					case 'p':
						putchar(current);
						next = scanFor("pre>", next);
						if (next > 0) {
							next = scanChar(next);
							if (next == '\n') {
								// todo: current may mess up things
								next = scanChar(getchar());
							}
							mode = 6;
						}
						else {
							next = -next;
						}
						current = next;
						next = getchar();
						break;
					case 's':
						putchar(current);
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
						current = next;
						next = getchar();
						break;
					}
				}
				else if (next == '/') {
					putchar(current);
					next = scanFor("/script>", next);
					if (next > 0) {
						html = 1;
					}
					else {
						next = -next;
					}
					current = next;
					next = getchar();
					break;
				}
				else {
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
							current = next;
							next = getchar();
						}
					}
				}
				/* fallthrough */
			case '&':			// no whitespaces between these and the rest
			case '*':			// no semicolon either when these are at the
			case '|':			// end of a line, most likely a broken up
			case ',':			// statement
			case ':':
				if (!html) {
					next = scanChar(next);
				}
				code = 0;
				break;
			case '>':
				if (html && (next == '\n')) {
					next = getchar();
				}
				if (!html) {
					next = scanChar(next);
				}
				code = 0;
				break;
			case '+':			// special case a++ will most likely need a ';'
				if (next == '+') {
					putchar('+');
					current = next;
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
					current = next;
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
				while (next == '\n') {
					next = (scanChar(getchar()));
				}
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
					current = ';';
					code = 0;
				}
				else if (html) {
					current = ' ';
				}
				else {
					current = 0;
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
				if ((html) && (next == ' ')) {
					current = 0;
				}
				else {
					switch (next) {
					case '>':
					case '=':
					case '+':
					case '-':
					case '*':
					case '(':
					case ',':
					case '<':
					case '{':
					case '&':
					case '|':
					case '\\':
					case ' ':
						code = 0;
						current = 0;
						break;
					case '/':
					case '}':
					case ')':
						current = 0;
						break;
					}
				}
				break;
			default:
				if (!html) {
					code = 1;
				}
			}
			break;
		case 1:				// line comment
			if (current == '\n') {
				mode = 0;
				if (code == 1)
					current = ';';
				else
					current = 0;
			}
			else {
				current = 0;
			}
			break;
		case 2:				// block comment
			if ((current == '*') && (next == '/')) {
				mode = 0;
				next = getchar();
			}
			current = 0;
			break;
		case 3:				// single quotes
			if (current == '\\') {
				if (next == '\n') {
					next = getchar();
				}
				else {
					putchar(current);
				}
				current = next;
				next = getchar();
			}
			else if (current == '\'') {
				mode = 0;
				code = 1;
			}
			break;
		case 4:				// double quotes
			if (current == '\\') {
				if (next == '\n') {
					next = getchar();
				}
				else {
					putchar(current);
				}
				current = next;
				next = getchar();
			}
			else if (current == '"') {
				mode = 0;
				code = 1;
			}
			break;
		case 5:
			if ((current == '-') && (next == '-')) {
				next = getchar();
				if (next == '>') {
					next = getchar();
					mode = 0;
				}
			}
			current = 0;
			break;
		case 6:
			if (current == '<') {
				putchar(current);
				next = scanFor("/pre>", next);
				if (next > 0) {
					mode = 0;
				}
				else {
					next = -next;
				}
				current = next;
			}
			break;
		}
		if (current > 0) {
			putchar(current);
		}
		current = next;
	}
	return 0;
}
