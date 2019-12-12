/*
 * json.c
 *
 *  Created on: 08.12.2017
 *	  Author: bweber
 */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "json.h"

static char _jsonError[512];
static int jsonFail( const char *msg, ... ) __attribute__((__format__(__printf__, 1, 2)));

/* forward definitions of cyclic dependencies in static functions */
static char *jsonWriteObj( jsonObject *jo, char *json, size_t *len );
static char *jsonWriteArr( jsonObject *jo, char *json, size_t *len );
static int jsonParseObject( char *json, jsonObject **jo );
static int jsonParseArray( char *json, jsonObject **jo );

/*
 * returns the current error message
 */
char *jsonGetError() {
	char *err=(char*)calloc( strlen(_jsonError)+1, 1 );
	if( err != NULL ) {
		strcpy( err, _jsonError );
	}
	_jsonError[0]=0;
	return err;
}

int jsonHasError() {
	return strlen( _jsonError );
}

/*
 * error handling functions
 */
static int jsonFail( const char *msg, ... ) {
	va_list args;
	va_start( args, msg );
	vsnprintf( _jsonError, 511, msg, args );
	va_end( args );
	fprintf( stderr, "JSON: %s\n", _jsonError );
	return -1;
}

/**
 * simple wrapper to keep old messages
 */
static int jsonParseFail( const char *func, const char *str, const int i, const int state ) {
	return jsonFail( "%s#%i: Found invalid '%c' in JSON pos %i\n%s", func, state, str[i], i, str );
}

/*
 * todo proper handling of \uXXXX
 */
static char *jsonEncode( const char *val ) {
	size_t len=0;
	char *ret=NULL;
	unsigned ip, op;

	if( val == NULL ) {
		return NULL;
	}

	/* guess length of target string */
	for( ip=0; ip<strlen(val); ip++ ) {
		switch( val[ip] ) {
		case '"':
		case '\\':
		case '/':
		case '\b':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
			len+=2;
			break;
		default:
			len++;
		}
	}

	ret=(char*)calloc( len+1, 1 );
	if( ret == NULL ) {
		jsonFail( "Out of memory!" );
		return NULL;
	}

	for( ip=0, op=0; ip<strlen(val); ip++ ) {
		switch( val[ip] ) {
		case '"':
			ret[op++]='\\';
			ret[op++]='"';
			break;
		case '\\':
			ret[op++]='\\';
			ret[op++]='\\';
			break;
		case '/':
			ret[op++]='\\';
			ret[op++]='/';
			break;
		case '\b':
			ret[op++]='\\';
			ret[op++]='b';
			break;
		case '\f':
			ret[op++]='\\';
			ret[op++]='f';
			break;
		case '\n':
			ret[op++]='\\';
			ret[op++]='n';
			break;
		case '\r':
			ret[op++]='\\';
			ret[op++]='r';
			break;
		case '\t':
			ret[op++]='\\';
			ret[op++]='t';
			break;
		/* no explicit encoding of extended chars yet! */
		default:
			ret[op++]=val[ip];
		}
	}
	ret[op]=0;

	return ret;
}

/*
 * todo proper handling of \uXXXX
 */
static int jsonDecodeInto( const char *val, char *ret, size_t len ) {
	unsigned ip=0;
	unsigned op=0;

	while( ( ip < strlen(val) ) && ( op < len-1) ) {
		if( val[ip]  == '\\' ) {
			ip++;
			switch( val[ip] ) {
			case '"':
				ret[op++]='"';
				break;
			case '\\':
				ret[op++]='\\';
				break;
			case '/':
				ret[op++]='/';
				break;
			case 'b':
				ret[op++]='\b';
				break;
			case 'f':
				ret[op++]='\f';
				break;
			case 'n':
				ret[op++]='\n';
				break;
			case 'r':
				ret[op++]='\r';
				break;
			case 't':
				ret[op++]='\t';
				break;
			default:
				jsonParseFail( __func__, val, ip, 0 );
				return -1;
			}
		}
		else {
			ret[op++]=val[ip];
		}
		ip++;
	}
	ret[op]=0;

	return strlen(ret);
}

/**
 * decode a JSON string value into a standard C text representation
 */
static char *jsonDecode( const char *val ) {
	size_t len=0;
	unsigned ip=0;
	char *ret=NULL;

	/* guess length of target string */
	for( ip=0; ip<strlen(val); ip++ ) {
		if( val[ip] != '\\' ) len++;
	}

	ret=(char*)calloc( len+1, 1 );
	if( ret == NULL ) {
		jsonFail( "Out of memory!" );
		return NULL;
	}

	jsonDecodeInto( val, ret, len+1 );

	return ret;
}

/**
 * creates an empty json node
 */
static jsonObject *jsonInit( void ) {
	jsonObject *jo=NULL;
	jo=(jsonObject*)calloc(1, sizeof(jsonObject));
	if( jo == NULL ) {
		jsonFail( "Out of memory!" );
		return NULL;
	}
	jo->key=NULL;
	jo->next=NULL;
	jo->val=NULL;
	jo->type=json_null;
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
					return jsonParseFail( __func__, json, i, state );
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
				return jsonParseFail( __func__, json, i, state );
			}
			break;
		}
		i++;
	}

	return -1;
}

/*
 * parsechecks a string value, this does no decoding!
 */
static int jsonParseString( char *json, char **val ) {
	int len=strlen(json);
	int ip=0;
	int j=0;
	int state=0;
	char *start=NULL;

	*val=NULL;
	while( ip < len ) {
		switch( state ) {
		case 0: /* outside */
			switch( json[ip] ) {
			case '"':
				state=1;
				start=json+ip+1;
				break;
			case ' ':
				break;
			default:
				return jsonParseFail( __func__, json, ip, state );
			}
			break;
		case 1: /* in quotes */
			switch( json[ip] ) {
			case '\\':
				state=2;
				break;
			case '"':
				json[ip]=0;
				*val=strdup(start);
				return ip+1;
				break;
			}
			break;
		case 2: /* escape */
			switch( json[ip] ) {
			case 'u': /* four digits follow */
				for( j=0; j<4; j++ ) {
					ip++;
					if( !isxdigit(json[ip] ) ) {
						return jsonParseFail( __func__, json, ip, state );
					}
				}
				/* fallthrough */
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
				return jsonParseFail( __func__, json, ip, state );
			}
			break;
		}
		ip++;
	}

	return -1;
}

/*
 * parses a JSON value and decides the value according to the JSON definition
 * " starts a string
 * { starts an object
 * [ starts an array
 * f is probably boolean false
 * t is probably boolean true
 * n is probably null
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
		jo->val=jsonInit();
		jpos+=jsonParseObject( &json[jpos], (jsonObject **)&(jo->val) );
		return jpos;
		break;
	case '[':
		jo->type=json_array;
		jo->val=jsonInit();
		jpos+=jsonParseArray( &json[jpos], (jsonObject **)&(jo->val) );
		return jpos;
		break;
	case 't':
		if( strstr( json+jpos, "true" ) == json+jpos ) {
			jo->type=json_boolean;
			jo->val=(void *)-1;
			jpos+=strlen("true");
			return jpos;
		}
		else {
			jsonParseFail( __func__, json, jpos, state );
		}
		break;
	case 'f':
		if( strstr( json+jpos, "false" ) == json+jpos ) {
			jo->type=json_boolean;
			jo->val=NULL;
			jpos+=strlen("false");
			return jpos;
		}
		else {
			jsonParseFail( __func__, json, jpos, state );
		}
		break;
	case 'n':
		if( strstr( json+jpos, "null" ) == json+jpos ) {
			jo->type=json_null;
			jo->val=NULL;
			jpos+=strlen("null");
			return jpos;
		}
		else {
			jsonParseFail( __func__, json, jpos, state );
		}
		break;
	default:
		if( isdigit( json[jpos] ) || json[jpos]=='-' ) {
			jo->type=json_number;
			jpos+=jsonParseNum( &json[jpos], (char **)&(jo->val) );
			return jpos;
		}
		else {
			jsonParseFail( __func__, json, jpos, state );
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

	*jo=jsonInit();

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
				return jsonParseFail( __func__, json, jpos, state );
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
				return jsonParseFail( __func__, json, jpos, state );
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
	jo->key=strdup(buf);
	if( jo->key == NULL ) {
		jsonFail( "Out of memory!" );
		return -1;
	}
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
				*current=jsonInit();
				if( *current==NULL ) {
					return jsonParseFail( __func__, json, jpos, state );
				}
				index=setIndex( *current, index );
				jpos+=jsonParseValue( json+jpos, *current );
				state=1;
				break;
			case ' ':
				jpos++;
				break;
			default:
				return jsonParseFail( __func__, json, jpos, state );
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
				(*current)->next=jsonInit();
				if( (*current)->next == NULL ) {
					return jsonParseFail( __func__, json, jpos, state );
				}
				current=&((*current)->next);
				index=setIndex( *current, index );
				jpos+=jsonParseValue( json+jpos, *current );
				break;
			default:
				return jsonParseFail( __func__, json, jpos, state );
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
				return jsonParseFail( __func__, json, jpos, state );
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
				return jsonParseFail( __func__, json, jpos, state );
			}
			break;
		}
	}

	return jpos;
}

static jsonObject *jsonFetch( jsonObject *jo, const char *key ) {
	jsonObject *target = jo;

	while( target != NULL ) {
		if( strcmp( target->key, key ) == 0 ) {
			return target;
		}
		target=target->next;
	}

	return target;
}

/*
 * resolves a JSON key path in dot notation
 */
static jsonObject *jsonFollowPath( jsonObject *jo, const char *key ) {
	char *path=strdup(key);
	char *hook=path;
	char *pos=strchr(path,'.');

	while( pos != NULL ) {
		*pos=0;
		pos=pos+1;
		jo=jsonFetch(jo,path);
		if( jo == NULL ){
			return NULL;
		}
		jo=(jsonObject*)jo->val;
		path=pos;
		pos=strchr(path,'.');
	}

	jo=jsonFetch(jo,path);
	free(hook);
	return jo;
}

/*
 * returns the jsonType of the current object
 * can also be used to check if an object exists - i.e. array indices
 */
jsonType jsonPeek( jsonObject *jo, char *key ) {
	jo=jsonFollowPath( jo, key );
	if( jo == NULL ) {
		return json_error;
	}
	return jo->type;
}

unsigned jsonGetBool( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_boolean ) ) {
		return( pos->val == (void*)-1 );
	}

	return 0;
}

/*
 * returns the int value of key
 */
int jsonGetInt( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_number ) ) {
		return atoi( (char*)pos->val );
	}

	return 0;
}

/**
 * copies and decodes the value of key into a new memory area
 * the memory will be allocated so make sure that returned pointer is free'd after use
 */
char *jsonGetStr( jsonObject *jo, const char *key ) {
	jo=jsonFollowPath( jo, key );
	if( jo == NULL ) {
		return NULL;
	}
	return jsonDecode( (char*)jo->val );
}

/*
 * copies the decoded value for key into buf.
 */
int jsonCopyStr( jsonObject *jo, const char *key, char buf[], size_t size ) {
	jo=jsonFollowPath( jo, key );
	if( jo == NULL ) {
		return -1;
	}
	return jsonDecodeInto( (char*)jo->val, buf, size );
}

/*
 * helper function to resolve a JSON array index
 */
static void *jsonGetByIndex( jsonObject *jo, int i ) {
	char buf[20];
	sprintf( buf, "%i", i );
	jo=jsonFollowPath( (jsonObject*)jo->val, buf );
	return (jo==NULL)?NULL:jo->val;
}

/**
 * copy the array of strings into the vals pointer
 */
char **jsonGetStrs( jsonObject *jo, const char *key, const int num ) {
	int i;
	char **vals=NULL;
	char *val;
	jsonObject *pos=NULL;

	pos=jsonFollowPath( jo, key );
	if( (pos != NULL ) && ( pos->type == json_array ) ) {
		vals=(char**)calloc( num, sizeof( char * ) );
		if( vals == NULL ) {
			jsonFail( "Out of memory!" );
			return NULL;
		}

		for( i=0; i<num; i++ ) {
			val=(char *)jsonGetByIndex( pos, i );
			if( val == NULL ) {
				vals[i]=NULL;
			}
			else {
				vals[i]=jsonDecode(val);
			}
		}
	}
	else {
		jsonFail( "No array for %s", key );
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
	if( ( pos != NULL ) && ( ( pos->type == json_object ) || ( pos->type == json_null ) ) ) {
		return (jsonObject*)pos->val;
	}
	else {
		jsonFail( "No object for key '%s'", key );
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
		jo=jsonInit();
	}
	else {
		while( jo->next != NULL ) {
			jo=jo->next;
		}
		jo->next=jsonInit();
		jo=jo->next;
	}

	/* make sure that the key can always be free'd */
	jo->key=strdup(key);
	if( jo->key == NULL ) {
		jsonFail( "Out of memory!" );
		return NULL;
	}

	return jo;
}

/**
 * creates a new JSON boolean object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddBool( jsonObject *jo, const char *key, const unsigned val ) {
	jo=jsonAppend( jo, key );
	if( val ) {
		jo->val=(void*)1;
	}
	else {
		jo->val=NULL;
	}
	jo->type=json_boolean;
	return jo;
}

/**
 * creates a new JSON array object with the values in val and appends it to the end of the given root object chain
 */
static jsonObject *jsonAddArr( jsonObject *jo, const char *key, jsonObject *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_array;
	jo->val=val;
	return jo;
}

/**
 * creates a new JSON string object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_string;
	if( val == NULL ) {
		jo->val=NULL;
	}
	else {
		jo->val=jsonEncode( val );
		if( jo->val == NULL ) {
			jsonFail( "Out of memory!" );
			return NULL;
		}
	}
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
	jo->val=calloc( strlen(buf)+1, sizeof(char) );
	if( jo->val == NULL ) {
		jsonFail( "Out of memory!" );
		return NULL;
	}
	strcpy( (char*)jo->val, buf );
	jo->type=json_number;
	return jo;
}

/**
 * creates a new JSON object object and appends it to the end of the given root object chain
 */
jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_object;
	if( val == NULL ) {
		jo->type=json_null;
	}
	jo->val=val;
	return jo;
}

/*
 * Adds a new element to a jsonArray chain.
 */
int jsonAddArrElement( jsonObject *jo, void *val, jsonType type ) {
	char key[20];
	int index=0;

	if( jo == NULL ) {
		jsonFail( "Cannot add an array Element to an empty object!" );
		return -1;
	}

	while( jo->next != NULL ) {
		jo=jo->next;
	}

	/* if the root object is the array object, switch to the values */
	if( jo->type != json_array ) {
		jsonFail( "No Array Object to add to!" );
		return -1;
	}

	if( jo->val != NULL ) {
		jo=(jsonObject*)jo->val;
		/* forward top the last element to get the highest index */
		while( jo->next != NULL ) {
			jo=jo->next;
		}
		index=atoi(jo->key)+1;
	}

	sprintf( key, "%i", index );

	if( index == 0 ) {
		switch( type ) {
		case json_array:
			jo->val=jsonAddArr(NULL, key, (jsonObject*)val);
			break;
		case json_number:
			jo->val=jsonAddInt(NULL, key, atoi((char*)val));
			break;
		case json_object:
			jo->val=(jsonObject*)val;
			if (((jsonObject*)jo->val)->key) {
				free(((jsonObject*)jo->val)->key);
			}
			((jsonObject*)jo->val)->key=strdup(key);
			break;
		case json_string:
			jo->val=jsonAddStr(NULL, key, (char*)val );
			break;
		case json_boolean:
			jo->val=jsonAddBool(NULL, key, (unsigned long)val );
			break;
		case json_null:
			jo->val=jsonAddObj(NULL, key, NULL );
			break;
		default:
			return 0;
		}
	} else {
		switch( type ) {
		case json_array:
			jsonAddArr(jo, key, (jsonObject*)val );
			break;
		case json_number:
			jsonAddInt(jo, key, atoi((char*)val));
			break;
		case json_object:
			jo->next = val;
			if(jo->next->key){
				free(jo->next->key);
			}
			jo->next->key=strdup(key);
			break;
		case json_string:
			jsonAddStr( jo, key, (char*)val );
			break;
		case json_boolean:
			jsonAddBool( jo, key, (unsigned long)val );
			break;
		case json_null:
			jsonAddObj(jo, key, NULL );
			break;
		default:
			return 0;
		}
	}
	return 1;
}

/*
 * creates an empty jsonArray into which items can be added with
 * jsonAddArrElement
 */
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
		jsonDiscard(jo);
		return NULL;
	}
}

static char *sizeCheck( char *json, size_t *len ) {
	char *ret=NULL;
	if( strlen( json ) > (*len)-JSON_LOWATER ) {
		*len=(*len)+JSON_INCBUFF;
		ret=(char*)realloc( json, *len );
		if( ret == NULL ) {
			jsonFail( "Out of Memory!" );
			return NULL;
		}
		json=ret;
	}
	return json;
}

/**
 * encodes a value into JSON notation
 * "strval"|numval|{objval}|[arr],[val]
 */
static char *jsonWriteVal( jsonObject *jo, char *json, size_t *len ) {
	switch( jo->type ) {
	case json_string:
		json=sizeCheck( json, len );
		strcat( json, "\"" );
		strcat( json, (char*)jo->val );
		strcat( json, "\"" );
		break;
	case json_number:
		strcat( json, (char*)jo->val );
		break;
	case json_object:
		json=jsonWriteObj( (jsonObject*)jo->val, json, len );
		break;
	case json_array:
		json=jsonWriteArr( (jsonObject*)jo->val, json, len );
		break;
	case json_boolean:
		if( jo->val == NULL ) {
			strcat( json, "false" );
		}
		else {
			strcat( json, "true" );
		}
		break;
	case json_null:
		strcat( json, "null" );
		break;
	case json_error:
		jsonFail("Illegal json_type set on %s", jo->key );
		strcat( json, "json_error" );
		break;
	}
	return json;
}

/**
 * encodes a stream of key,value tupels into JSON notation
 * "key":value[,"key":value]*
 */
static char *jsonWriteKeyVal( jsonObject *jo, char *json, size_t *len ) {
	while( jo != NULL) {
		json=sizeCheck( json, len );
		strcat( json, "\"" );
		strcat( json, jo->key );
		strcat( json, "\":" );

		json=jsonWriteVal( jo, json, len );

		if( jo->next != NULL ) {
			strcat( json, "," );
		}
		jo=jo->next;
	}
	return json;
}

/**
 * encodes an array into JSON notation
 * [val],[val],...
 */
static char *jsonWriteArr( jsonObject *jo, char *json, size_t *len ) {
	json=sizeCheck( json, len );
	strcat( json, "[" );
	while( jo != NULL ) {
		json=jsonWriteVal( jo, json, len );
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
static char *jsonWriteObj( jsonObject *jo, char *json, size_t *len ) {
	json=sizeCheck( json, len );
	strcat( json, "{" );
	json=jsonWriteKeyVal( jo, json, len );
	strcat( json, "}" );
	return json;
}

/*
 * turns the jsonObject tree into a json string and disposes of the jsonObject tree
 */
char *jsonToString( jsonObject *jo ) {
	char *json=NULL;
	size_t len=JSON_INCBUFF;
	json=(char*)calloc( len, 1 );
	if( json == NULL ) {
		jsonFail( "Out of memory!" );
		return NULL;
	}
	json=jsonWriteObj( jo, json, &len );
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
			jsonDiscard( (jsonObject*)jo->val );
			jo->val=NULL;
		}

		pos=jo->next;

		free( jo->key );
		jo->key=NULL;
		/* free value if it's a string, other complex values have been freed */
		if( ( jo->type == json_string ) &&
				( jo->val != NULL ) ) {
			free( jo->val );
			jo->val=NULL;
		}
		free( jo );

		jo=pos;
	}
}
