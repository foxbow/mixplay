/*
 * json.h
 *
 *  Created on: 08.12.2017
 *	  Author: bweber
 */

#ifndef _JSON_H_
#define _JSON_H_

#include <stddef.h>

/* if less than JSON_LOWATER bytes are unused in the string it will be
 * increased by JSON_INCBUFF bytes
 */
#define JSON_LOWATER 128
#define JSON_INCBUFF 512
/*
 * JSON defined datatypes
 */
typedef enum  {
	json_null,
	json_boolean,
	json_string,
	json_number,
	json_object,
	json_array
} jsonType;

typedef struct  jsonObject_s jsonObject;
/*
 * key, val - the JSON key and value pair
 * type	 - JSON type of val
 * ref	  - 0 - key and val may be free'd
 *			1 - only key can be free'd
 *			2 - neither key nor val shall be free'd
 * next	 - next json pair
 */
struct jsonObject_s{
	char *key;
	void *val;
	jsonType type;
	int ref;
	jsonObject *next;
};

int jsonHasError( void );
char *jsonGetError( void );

jsonType jsonPeek( jsonObject *jo, char *key );
int	jsonGetInt( jsonObject *jo, const char *key );
int	jsonCopyStr( jsonObject *jo, const char *key, char *buf, size_t len );
char  *jsonGetStr( jsonObject *jo, const char *key );
char **jsonGetStrs( jsonObject *jo, const char *key, const int num );
unsigned jsonGetBool( jsonObject *jo, const char *key );
jsonObject *jsonGetObj( jsonObject *jo, const char *key );

jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val );
jsonObject *jsonAddStrs( jsonObject *jo, const char *key, char **vals, const int num );
jsonObject *jsonAddInt( jsonObject *jo, const char *key, const int val );
jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val );
jsonObject *jsonAddArr( jsonObject *jo, const char *key, jsonObject *val );
jsonObject *jsonAddBool( jsonObject *jo, const char *key, const unsigned val );

jsonObject *jsonInitArr( jsonObject *jo, const char *key );
jsonObject *jsonAddArrElement( jsonObject *jo, jsonType type, void *element );

jsonObject *jsonRead( char *json );
char *jsonToString( jsonObject *jo );

void jsonDiscard( jsonObject *jo );
#endif /* _JSON_H_ */
