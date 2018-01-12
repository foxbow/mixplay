/*
 * json.h
 *
 *  Created on: 08.12.2017
 *      Author: bweber
 */

#ifndef _JSON_H_
#define _JSON_H_

enum _jsonTypes_t {
	json_none,
	json_string,
	json_number,
	json_object,
	json_array
};

typedef enum _jsonTypes_t jsonType;

typedef struct _jsonObject_t jsonObject;

struct _jsonObject_t {
	char *key;
	void *val;
	jsonType type;
	jsonObject *next;
};


int   jsonGetInt( jsonObject *jo, const char *key );
const char *jsonGetStr( jsonObject *jo, const char *key );
int jsonCopyChars( jsonObject *jo, const char *key, char *buf );
int jsonCopyStr( jsonObject *jo, const char *key, char **buf );
int jsonCopyStrs( jsonObject *jo, const char *key, char ***vals, const int num );
jsonObject *jsonGetObj( jsonObject *jo, const char *key );

jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val );
jsonObject *jsonAddStrs( jsonObject *jo, const char *key, char **vals, const int num );
jsonObject *jsonAddInt( jsonObject *jo, const char *key, const int val );
jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val );

jsonObject *jsonRead( char *json );
size_t jsonWrite( jsonObject *jo, char *json );

void jsonDiscard( jsonObject *jo, int all );
#endif /* _JSON_H_ */
