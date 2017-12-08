/*
 * json.h
 *
 *  Created on: 08.12.2017
 *      Author: bweber
 */

#ifndef _JSON_H_
#define _JSON_H_

enum jsonTypes_t {
	none,
	string,
	number,
	object,
	array
};

typedef enum jsonTypes_t jsonType;

struct tupel_t {
	char *key;
	void *val;
	jsonType type;
	struct tupel_t *next;
};

typedef struct tupel_t jsonObject;

jsonObject *jsonParse( char *json );
int   jsonGetInt( jsonObject *jo, const char *key );
char *jsonGetStr( jsonObject *jo, const char *key );
int jsonCopyStr( jsonObject *jo, const char *key, char *buf );
jsonObject *jsonGetObj( jsonObject *jo, const char *key );

jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val );
jsonObject *jsonAddInt( jsonObject *jo, const char *key, const int val );
jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val );
size_t jsonWrite( jsonObject *jo, char *json );

void jsonDiscard( jsonObject *jo, int all );
#endif /* _JSON_H_ */
