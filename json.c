/*
 * json.c
 *
 *  Created on: 08.12.2017
 *      Author: bweber
 */
#include "config.h"
#include "utils.h"
#include "json.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* forward definitions of cyclic dependencies in static functions */
static char *jsonWriteObj( jsonObject *jo, char *json, int len );
static char *jsonWriteArr( jsonObject *jo, char *json, int len );
static int jsonParseObject( char *json, jsonObject **jo );
static int jsonParseArray( char *json, jsonObject **jo );

/*
 * error handling function
 * todo: make this generic so the json functions can be used outside of mixplay
 */
static int jsonFail( const char *func, char *str, const int i, const int state ) {
	addMessage( 0, "%s#%i: Found invalid '%c' in JSON pos %i\n%s", func, state, str[i], i, str );
	return -1;
}

/**
 * creates an empty json node
 * if ref is 0 this means the content is owned by the node if not
 * it's a reference and must not be free'd
 * ref: 0 - neither key nor val are to be free'd
 *      1 - only key needs to be free'd
 *      2 - key and val are to be free'd
 */
static jsonObject *jsonInit( int ref ) {
	jsonObject *jo;
	jo=falloc(1, sizeof(jsonObject));
	jo->key=NULL;
	jo->next=NULL;
	jo->val=NULL;
	jo->type=json_none;
	jo->ref=ref;
	return jo;
}

/*
 * parses a number into an int value
 * we allow leading zeroes, even if JSON forbids that
 */
static int jsonParseNum( char *json, char **val ) {
	int len=strlen(json);
	int i=0;
	int state=0;
	*val=json;

	while( i<len ) {
		switch( state ) {
		case 0: /* looking for a number */
			switch( json[i] ) {
			case ' ': /* unlikely to happen */
				(*val)++;
				break;
			case '-':
				state=1;
				break;
			default:
				if( isdigit( json[i]) ) {
					state=1;
				}
				else {
					return jsonFail( __func__, json, i, state );
				}
			}
			break;
		case 1: /* prefix */
			if( isdigit( json[i] ) ) {
			}
			else {
				state=2;
			}
			break;
		case 2:
			switch( json[i] ) {
			case '.':
				state=3;
				break;
			case 'e':
			case 'E':
				state=4;
				break;
			default:
				return i-1;
			}
			break;
		case 3:
			if( !isdigit( json[i] ) ) {
				return i-1;
			}
			break;
		case 4:
			if( isdigit( json[i] ) || ( json[i]=='+' ) || ( json[i]=='-' ) ) {
				state=3;
			}
			else {
				return jsonFail( __func__, json, i, state );
			}
			break;
		}
		i++;
	}

	return -1;
}

/*
 * parses a string value
 */
static int jsonParseString( char *json, char **val ) {
	int len=strlen(json);
	int i=0;
	int state=0;

	*val=NULL;
	while( i<len ) {
		switch( state ) {
		case 0:
			switch( json[i] ) {
			case '"':
				state=1;
				*val=json+i+1;
				break;
			case ' ':
				break;
			default:
				return jsonFail( __func__, json, i, state );
			}
			break;
		case 1: /* in quotes */
			switch( json[i] ) {
			case '\\':
				state=2;
				break;
			case '"':
				json[i]=0;
				return i+1;
				break;
			}
			break;
		case 2: /* escape */
			switch( json[i] ) {
			case 'u':
				i+=4; /* todo are we really skipping hex numbers? */
				/* no break */
			case '"':
			case '\\':
			case '/':
			case 'b':
			case 'f':
			case 'n':
			case 'r':
			case 't':
				state=1;
				break;
			default:
				return jsonFail( __func__, json, i, state );
			}
			break;
		}
		i++;
	}

	return -1;
}

/*
 * parses a JSON value and decides the value according to the JSON definition
 * " starts a string
 * { starts an object
 * [ starts an array
 * everything else is either a number or illegal.
 */
static int jsonParseValue( char *json, jsonObject *jo ) {
	int jpos=0;
	int state=0;

	switch( json[jpos] ) {
	case ' ':
		jpos++;
		break;
	case '"':
		jo->type=json_string;
		jpos+=jsonParseString( &json[jpos], (char **)&(jo->val) );
		return jpos;
		break;
	case '{':
		jo->type=json_object;
		jo->val=jsonInit(0);
		jpos+=jsonParseObject( &json[jpos], (jsonObject **)&(jo->val) );
		return jpos;
		break;
	case '[':
		jo->type=json_array;
		jo->val=jsonInit(0);
		jpos+=jsonParseArray( &json[jpos], (jsonObject **)&(jo->val) );
		return jpos;
		break;
	default:
		if( isdigit( json[jpos] ) || json[jpos]=='-' ) {
			jo->type=json_number;
			jpos+=jsonParseNum( &json[jpos], (char **)&(jo->val) );
			return jpos;
		}
		else {
			jsonFail( __func__, json, jpos, state );
		}
	}
	return -1;
}

/*
 * parses a JSON key,value tupel
 */
static int jsonParseKeyVal( char *json, jsonObject **jo ) {
	int jpos=0;
	int state=0;
	int len=strlen(json);

	*jo=jsonInit(0);

	while( jpos < len ) {
		switch( state ) {
		case 0: /* find key */
			switch( json[jpos] ) {
			case ' ':
				jpos++;
				break;
			case '"':
				jpos+=jsonParseString( &json[jpos], &((*jo)->key) );
				state=1;
				break;
			default:
				return jsonFail( __func__, json, jpos, state );
			}
			break;
		case 1: /* got key */
			switch( json[jpos] ) {
			case ':':
				state=2;
				jpos++;
				break;
			case ' ':
				jpos++;
				break;
			default:
				return jsonFail( __func__, json, jpos, state );
			}
			break;
		case 2: /* get value */
			jpos+=jsonParseValue( &json[jpos], *jo );
			return jpos;
		}
	}

	return -1;
}

/*
 * helperfunction to set numeric indices on array objects
 */
static int setIndex( jsonObject *jo, int i ) {
	char buf[20];
	sprintf( buf, "%i", i );
	jo->key=falloc( strlen(buf)+1, sizeof(char) );
	strcpy( jo->key, buf );
	return i+1;
}

/*
 * parses a JSON array
 */
static int jsonParseArray( char *json, jsonObject **jo ) {
	int jpos=0;
	int state=0;
	int len=strlen(json);
	int index=0;
	jsonObject **current=jo;

	while( jpos < len ) {
		switch( state ) {
		case 0: /* Parse first value */
			switch( json[jpos] ) {
			case '[':
				jpos++;
				*current=jsonInit(1);
				index=setIndex( *current, index );
				jpos+=jsonParseValue( json+jpos, *current );
				state=1;
				break;
			case ' ':
				jpos++;
				break;
			default:
				return jsonFail( __func__, json, jpos, state );
			}
			break;
		case 1: /* next value? */
			switch( json[jpos] ){
			case ' ':
				json[jpos]=0;
				jpos++;
				break;
			case ']':
				json[jpos]=0;
				return jpos+1;
				break;
			case ',':
				json[jpos]=0;
				jpos++;
				(*current)->next=jsonInit(1);
				current=&((*current)->next);
				index=setIndex( *current, index );
				jpos+=jsonParseValue( json+jpos, *current );
				break;
			default:
				return jsonFail( __func__, json, jpos, state );
			}
			break;
		}
	}

	return jpos;
}

/*
 * parses a JSON object
 */
static int jsonParseObject( char *json, jsonObject **jo ) {
	int jpos=0;
	int state=0;
	int len=strlen(json);
	jsonObject **current=jo;

	while( jpos < len ) {
		switch( state ) {
		case 0: /* Parse first value */
			switch( json[jpos] ) {
			case '{':
				jpos++;
				jpos+=jsonParseKeyVal( json+jpos, current );
				state=1;
				break;
			case ' ':
				jpos++;
				break;
			default:
				return jsonFail( __func__, json, jpos, state );
			}
			break;
		case 1: /* next value? */
			switch( json[jpos] ){
			case ' ':
				json[jpos]=0;
				jpos++;
				break;
			case '}':
				json[jpos]=0;
				return jpos+1;
				break;
			case ',':
				json[jpos]=0;
				jpos++;
				jpos+=jsonParseKeyVal(json+jpos, &((*current)->next));
				current=&((*current)->next);
				break;
			default:
				return jsonFail( __func__, json, jpos, state );
			}
			break;
		}
	}

	return jpos;
}

/*
 * resolves a JSON key path in dot notation
 */
static jsonObject *jsonFollowPath( jsonObject *jo, const char *key ) {
	jsonObject *target=jo;
	char *path;
	const char *pos;

	if( strchr( key, '.' ) != NULL ) {
		path=falloc( strlen( key )+1, sizeof( char ) );
		strcpy( path, key );
		strchr( path, '.' )[0]=0;
		target=jsonFollowPath( jo, path );
		free( path );
	}

	pos=strrchr( key, '.' );
	if( pos == NULL ){
		pos=key;
	}

	while( target != NULL ) {
		if( strcmp( target->key, pos ) == 0 ) {
			return target;
		}
		target=target->next;
	}

	return target;
}

/*
 * returns the int value of key
 */
int jsonGetInt( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_number ) ) {
		return atoi( pos->val );
	}

	return 0;
}

/*
 * returns the string value of key
 * this is a pointer to the string, do *not* free or change!
 */
const char *jsonGetStr( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_string ) ) {
		return pos->val;
	}

	addMessage( 1, "No string value for key %s", key );
	return "";
}

/**
 * copies the value of key into the char* given by buf
 * the memory will be allocated so make sure that *buf is free'd after use
 */
char *jsonCopyStr( jsonObject *jo, const char *key ) {
	char *buf=NULL;
	const char *val=jsonGetStr(jo, key);
	buf=falloc( strlen( val )+1, sizeof( char ) );
	strcpy( buf, val );
	return buf;
}

/*
 * copies the value for key into buf.
 * Caveat: absolutely no range checking happens here!
 */
int jsonCopyChars( jsonObject *jo, const char *key, char buf[] ) {
	strcpy( buf, jsonGetStr(jo, key) );
	return strlen(buf);
}

/*
 * helper function to resolve a JSON array index
 */
static void *jsonGetByIndex( jsonObject *jo, int i ) {
	char buf[20];
	jsonObject *val=NULL;

	sprintf( buf, "%i", i );
	val=jsonFollowPath( jo->val, buf );
	if( val != NULL ) {
		return val->val;
	}
	else {
		addMessage( 1, "No index %i in %s", i, jo->key );
	}
	return NULL;
}

/**
 * copy the array of strings into the vals pointer
 */
char **jsonCopyStrs( jsonObject *jo, const char *key, const int num ) {
	int i;
	char **vals=NULL;
	char *val;
	jsonObject *pos=NULL;

	pos=jsonFollowPath( jo, key );
	if( (pos != NULL ) && ( pos->type == json_array ) ) {
		vals=falloc( num, sizeof( char * ) );

		for( i=0; i<num; i++ ) {
			val=(char *)jsonGetByIndex( pos, i );
			vals[i]=falloc( strlen(val)+1, sizeof( char ) );
			strcpy( vals[i], val );
		}
	}
	else {
		addMessage( 1, "No array value for %s", key );
	}
	return vals;
}

/*
 * returns the jsonObject at the path key
 * The path follows the dot notation, array elements are numbered sub-elements
 */
jsonObject *jsonGetObj( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_object ) ) {
		return pos->val;
	}

	return NULL;
}

/*
 * helperfunction to append a new jsonObject to jo
 * if jo==NULL then a new root object will be created
 * used by the the jsonAdd*() functions
 */
static jsonObject *jsonAppend( jsonObject *jo, const char *key ) {
	if( jo == NULL ) {
		jo=jsonInit(2);
	}
	else {
		while( jo->next != NULL ) {
			jo=jo->next;
		}
		jo->next=jsonInit(2);
		jo=jo->next;
	}

	jo->key=falloc( strlen(key)+1, sizeof(char) );
	strcpy( jo->key, key );

	return jo;
}

/**
 * filters out double quotes
 * todo: Proper encoding of special chars!
 */
static char *fixstr( char *target, const char *src ) {
	int i, j;
	for( i=0, j=0; i<strlen( src ); i++ ) {
		if( src[i] != '"' ) {
			target[j]=src[i];
			j++;
		}
	}
	target[j]=0;
	return target;
}

/**
 * creates a new JSON string object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val ) {
	jo=jsonAppend( jo, key );
	jo->val=falloc( strlen(val)+1, sizeof(char) );
	jo->type=json_string;
	fixstr( jo->val, val );
	return jo;
}

/**
 * creates a new JSON array object with the values in val and appends it to the end of the given root object chain
 */
jsonObject *jsonAddArr( jsonObject *jo, const char *key, jsonObject *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_array;
	jo->val=val;
	return jo;
}

/**
 * creates a new JSON (string) array object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddStrs( jsonObject *jo, const char *key, char **vals, const int num ) {
	jsonObject *buf=NULL;
	jsonObject *val=NULL;
	char buffer[20];
	int i;

	for( i=0; i<num; i++ ) {
		sprintf( buffer, "%i", i );
		buf=jsonAddStr( buf, buffer, vals[i] );
		if( i == 0 ) {
			val=buf;
		}
	}

	return jsonAddArr( jo, key, val );
}

/**
 * creates a new JSON integer (number) object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddInt( jsonObject *jo, const char *key, const int val ) {
	char buf[64];
	jo=jsonAppend( jo, key );
	sprintf( buf, "%i", val );
	jo->val=falloc( strlen(buf)+1, sizeof(char) );
	strcpy( jo->val, buf );
	jo->type=json_number;
	return jo;
}

/**
 * creates a new JSON object object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_object;
	jo->val=val;
	return jo;
}

/*
 * Adds a new element to a jsonArray chain.
 */
jsonObject *jsonAddArrElement( jsonObject *jo, jsonType type, void *val ) {
	char key[20];
	int index=0;

	/* if the root object is the array object, switch to the values */
	if( jo->type == json_array ) {
		jo=jo->val;
	}

	if( jo != NULL ) {
		/* forward top the last element to get the highest index */
		while( jo->next != NULL ) {
			jo=jo->next;
		}
		index=atoi(jo->key)+1;
	}
	sprintf( key, "%i", index );

	switch( type ) {
	case json_array:
		jo=jsonAddArr(jo, key, val );
		break;
	case json_number:
		jo=jsonAddInt(jo, key, atoi(val));
		break;
	case json_object:
		jo=jsonAddObj(jo, key, val);
		break;
	case json_string:
		jo=jsonAddStr( jo, key, val );
		break;
	default:
		return jo;
		/* error */
	}

	return jo;
}

jsonObject *jsonInitArr( jsonObject *jo, const char *key ) {
	jo=jsonAppend( jo, key );
	jo->type=json_array;
	return jo;
}

/**
 * parses the given JSON string into a tree of jsonObjects
 */
jsonObject *jsonRead( char *json ) {
	jsonObject *jo=NULL;
	if( jsonParseObject( json, &jo ) >= 0 ){
		return jo;
	}
	else {
		return NULL;
	}
}

static char *sizeCheck( char *json, int *len ) {
	if( strlen( json ) > *len-JSON_LOWATER ) {
		*len=*len+JSON_INCBUFF;
		json=frealloc( json, *len );
	}
	return json;
}

/**
 * encodes a value into JSON notation
 * "strval"|numval|{objval}|[arr],[val]
 */
static char *jsonWriteVal( jsonObject *jo, char *json, int len ) {
	switch( jo->type ) {
	case json_string:
		json=sizeCheck( json, &len );
		strcat( json, "\"" );
		strcat( json, jo->val );
		strcat( json, "\"" );
		break;
	case json_number:
		strcat( json, jo->val );
		break;
	case json_object:
		json=jsonWriteObj( jo->val, json, len );
		break;
	case json_array:
		json=jsonWriteArr( jo->val, json, len );
		break;
	case json_none:
		break;
	}
	return json;
}

/**
 * encodes a key,value tupel into JSON notation
 * "key",value
 */
static char *jsonWriteKeyVal( jsonObject *jo, char *json, int len ) {
	json=sizeCheck( json, &len );
	strcat( json, "\"" );
	strcat( json, jo->key );
	strcat( json, "\":" );

	json=jsonWriteVal( jo, json, len );

	if( jo->next != NULL ) {
		strcat( json, "," );
		json=jsonWriteKeyVal( jo->next, json, len );
	}

	return json;
}

/**
 * encodes an array into JSON notation
 * [val],[val],...
 */
static char *jsonWriteArr( jsonObject *jo, char *json, int len ) {
	json=sizeCheck( json, &len );
	strcat( json, "[" );
	while( jo != NULL ) {
		jsonWriteVal( jo, json, len );
		jo=jo->next;
		if( jo != NULL ) {
			strcat( json, "," );
		}
	}
	strcat( json, "]" );
	return json;
}

/**
 * encodes an object into JSON notation
 * {key,value}
 */
static char *jsonWriteObj( jsonObject *jo, char *json, int len ) {
	json=sizeCheck( json, &len );
	strcat( json, "{" );
	jsonWriteKeyVal( jo, json, len );
	strcat( json, "}" );
	return json;
}

/*
 * turns the jsonObject tree into a json string and disposes of the jsonObject tree
 */
char *jsonToString( jsonObject *jo ) {
	char *json=NULL;
	int len=JSON_INCBUFF;
	json=falloc( len, sizeof( char ) );
	jsonWriteObj( jo, json, len );
	jsonDiscard( jo );
	return json;
}

/**
 * cleans up a tree of json objects.
 */
void jsonDiscard( jsonObject *jo ) {
	jsonObject *pos=jo;

	while( jo != NULL ) {
		/* clean up values of complex types first */
		if( ( jo->type == json_object ) || ( jo->type == json_array ) ) {
			jsonDiscard( jo->val );
			jo->val=NULL;
		}

		pos=jo->next;

		/* key and value can be free'd */
		if( jo->ref == 2 ) {
			sfree( &(jo->key) );
			sfree( (char **)&(jo->val) );
		}
		/* only key can be free'd */
		else if( jo->ref == 1 ) {
			sfree( &(jo->key) );
		}
		sfree( (char **)&jo );

		jo=pos;
	}
}
