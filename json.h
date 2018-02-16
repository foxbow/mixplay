/*
 * json.h
 *
 *  Created on: 08.12.2017
 *      Author: bweber
 */

#ifndef _JSON_H_
#define _JSON_H_

/* if less than JSON_LOWATER bytes are unused in the string it will be
 * increased by JSON_INCBUFF bytes
 */
#define JSON_LOWATER 128
#define JSON_INCBUFF 512
/*
 * JSON defined datatypes
 */
enum _jsonTypes_t {
	json_none,
	json_string,
	json_number,
	json_object,
	json_array
};

typedef enum _jsonTypes_t jsonType;

typedef struct _jsonObject_t jsonObject;

/*
 * key, val - the JSON key and value pair
 * type     - JSON type of val
 * ref      - 0 - key and val may be free'd
 *            1 - only key can be free'd
 *            2 - neither key nor val shall be free'd
 * next     - next json pair
 */
struct _jsonObject_t {
	char *key;
	void *val;
	jsonType type;
	int ref;
	jsonObject *next;
};


int    jsonGetInt( jsonObject *jo, const char *key );
const char *jsonGetStr( jsonObject *jo, const char *key );
int    jsonCopyChars( jsonObject *jo, const char *key, char *buf );
char  *jsonCopyStr( jsonObject *jo, const char *key );
char **jsonCopyStrs( jsonObject *jo, const char *key, const int num );
jsonObject *jsonGetObj( jsonObject *jo, const char *key );

jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val );
jsonObject *jsonAddStrs( jsonObject *jo, const char *key, char **vals, const int num );
jsonObject *jsonAddInt( jsonObject *jo, const char *key, const int val );
jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val );
jsonObject *jsonAddArr( jsonObject *jo, const char *key, jsonObject *val );

jsonObject *jsonInitArr( jsonObject *jo, const char *key );
jsonObject *jsonAddArrElement( jsonObject *jo, jsonType type, void *element );

jsonObject *jsonRead( char *json );
char *jsonToString( jsonObject *jo );

void jsonDiscard( jsonObject *jo );
#endif /* _JSON_H_ */
