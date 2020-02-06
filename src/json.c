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

int jsonFail( jsonObject *jo, const char *msg, ... ) __attribute__((__format__(__printf__, 2, 3)));

/* forward definitions of cyclic dependencies in static functions */
static char *jsonWriteObj( jsonObject *jo, char *json, size_t *len );
static char *jsonWriteArr( jsonObject *jo, char *json, size_t *len );
static int jsonParseObject( char *json, jsonObject **jo );
static int jsonParseArray( char *json, jsonObject **jo );

/*
 * error handling functions
 */
int jsonFail( jsonObject *jo, const char *msg, ... ) {
	char jsonError[1025];
	va_list args;
	if( jo == NULL ) {
		return -1;
	}
	va_start( args, msg );
	vsnprintf( jsonError, 1024, msg, args );
	va_end( args );
	jsonAddStr( jo, "jsonError", jsonError );
	return 0;
}

/**
 * simple wrapper to keep old messages
 */
int jsonParseFail( jsonObject *jo, const char *func, const char *str, const int i, const int state ) {
	if( jo == NULL ) {
		return -1;
	}
	jsonFail( jo, "%s#%i: Found invalid '%c' in JSON pos %i", func, state, str[i], i );
	jsonFail( jo, "%s", str);
	return 0;
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
	assert( ret != NULL );

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
			/* filter out unprintable characters */
			if( !iscntrl(val[ip]) ) {
				ret[op++]=val[ip];
			}
#if JSON_DEBUG
			else {
				printf("Unprintable %c (%u) in %s\n", val[ip], (unsigned char)val[ip], val);
			}
#endif
			/* todo enable \uXXXX encoding */
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
	assert( ret != NULL );

	jsonDecodeInto( val, ret, len+1 );

	return ret;
}

/**
 * creates an empty json node
 */
static jsonObject *jsonInit( void ) {
	jsonObject *jo=NULL;
	jo=(jsonObject*)calloc(1, sizeof(jsonObject));
	assert( jo != NULL );
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
	int len=64<strlen(json)?64:strlen(json);
	int i=0;
	int state=0;
	char *start=json;
	char buf[65]="";

	while( i<len ) {
		switch( state ) {
		case 0: /* looking for a number */
			switch( json[i] ) {
			case ' ': /* unlikely to happen */
				start++;
			break;
			case '-':
			case '+':
				state=1;
				break;
			default:
				if( isdigit( json[i]) ) {
					state=1;
				}
				else {
					/* We found a non-number */
					*val=strdup("");
					return -1;
				}
			}
			break;
		case 1: /* prefix digits */
			if( !isdigit( json[i] ) ) {
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
				strncpy(buf,start,i-1);
				buf[i]=0;
				*val=strdup(buf);
				return i-1;
			}
			break;
		case 3:
			if( !isdigit( json[i] ) ) {
				strncpy(buf,start,i-1);
				buf[i]=0;
				*val=strdup(buf);
				return i-1;
			}
			break;
		case 4:
			if( isdigit( json[i] ) || ( json[i]=='+' ) || ( json[i]=='-' ) ) {
				state=3;
			}
			else {
				/* number format error */
				*val=strdup("");
				return -1;
			}
			break;
		}
		i++;
	}

	*val=strdup("");
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
				return -1;
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
						return -1;
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
				return -1;
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
	int rv=0;

	switch( json[jpos] ) {
	case ' ':
		jpos++;
		break;
	case '"':
		jo->type=json_string;
		rv=jsonParseString( &json[jpos], (char **)&(jo->val) );
		if( rv == -1 ) {
			jsonFail(jo, "Could not parse string at %i", jpos);
			jsonFail(jo, "%s", json);
			return -1;
		}
		jpos+=rv;
		return jpos;
		break;
	case '{':
		jo->type=json_object;
		rv=jsonParseObject( &json[jpos], (jsonObject **)&(jo->val) );
		if( rv == -1 ) {
			jsonFail(jo, "Could not parse Object at %i", jpos);
			jsonFail(jo, "%s", json);
			return -1;
		}
		jpos+=rv;
		return jpos;
		break;
	case '[':
		jo->type=json_array;
		rv=jsonParseArray( &json[jpos], (jsonObject **)&(jo->val) );
		if( rv == -1 ) {
			jsonFail(jo, "Could not parse Array at %i", jpos);
			jsonFail(jo, "%s", json);
			return -1;
		}
		jpos+=rv;
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
			jsonParseFail( jo, __func__, json, jpos, state );
			return -1;
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
			jsonParseFail( jo,__func__, json, jpos, state );
			return -1;
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
			jsonParseFail( jo,__func__, json, jpos, state );
			return -1;
		}
		break;
	default:
		if( isdigit( json[jpos] ) || json[jpos]=='-' ) {
			jo->type=json_number;
			/* tricky to notify an error here.. */
			jpos+=jsonParseNum( &json[jpos], (char **)&(jo->val) );
			return jpos;
		}
		else {
			jsonParseFail( jo,__func__, json, jpos, state );
			return -1;
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
	int rv;

	*jo=jsonInit();

	while( jpos < len ) {
		switch( state ) {
		case 0: /* find key */
			switch( json[jpos] ) {
			case ' ':
				jpos++;
				break;
			case '"':
				rv=jsonParseString( &json[jpos], &((*jo)->key) );
				if(rv == -1) {
					jsonFail(*jo, "Could not parse key at %i", jpos);
					jsonFail(*jo, "%s", json );
					return -1;
				}
				jpos+=rv;
				state=1;
				break;
			default:
				jsonParseFail( *jo, __func__, json, jpos, state );
				return -1;
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
				jsonParseFail( *jo, __func__, json, jpos, state );
				return -1;
			}
			break;
		case 2: /* get value */
			rv=jsonParseValue( &json[jpos], *jo );
			if( rv == -1 ) {
				return -1;
			}
			jpos+=rv;
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
	assert(jo->key != NULL );
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
				/* empty array! */
				if( json[jpos] == ']' ) {
					return jpos+1;
				}
				*current=jsonInit();
				assert( *current!=NULL );
				index=setIndex( *current, index );
				jpos+=jsonParseValue( json+jpos, *current );
				state=1;
				break;
			case ' ':
				jpos++;
				break;
			default:
				jsonParseFail( *jo, __func__, json, jpos, state );
				return -1;
			}
			break;
		case 1: /* next value? */
			switch( json[jpos] ){
			case ' ':
				jpos++;
				break;
			case ']':
				return jpos+1;
				break;
			case ',':
				jpos++;
				(*current)->next=jsonInit();
				assert((*current)->next != NULL );
				current=&((*current)->next);
				index=setIndex( *current, index );
				jpos+=jsonParseValue( json+jpos, *current );
				break;
			default:
				jsonParseFail( *jo, __func__, json, jpos, state );
				return -1;
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
	int rv=0;

	while( jpos < len ) {
		switch( state ) {
		case 0: /* Parse first value */
			switch( json[jpos] ) {
			case '{':
				jpos++;
				rv=jsonParseKeyVal( json+jpos, current );
				if( rv == -1 ) {
					return -1;
				}
				jpos+=rv;
				state=1;
				break;
			case ' ':
				jpos++;
				break;
			default:
				jsonParseFail( *jo, __func__, json, jpos, state );
				return -1;
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
				rv=jsonParseKeyVal(json+jpos, &((*current)->next));
				if( rv == -1 ) {
					return -1;
				}
				jpos+=rv;
				current=&((*current)->next);
				break;
			default:
				jsonParseFail( *jo, __func__, json, jpos, state );
				return -1;
			}
			break;
		}
	}

	return jpos;
}

static jsonObject *jsonFetch( jsonObject *jo, const char *key ) {
	jsonObject *target = jo;

	while( target != NULL ) {
		if( target->key && ( strcmp( target->key, key ) == 0 )) {
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
			free(hook);
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
 * looks for an errormessage and ignores the path to it.
 */
static jsonObject *jsonFindKey( jsonObject *jo, const char *key ) {
	jsonObject *knot=NULL;

	while( jo != NULL ) {
		if ( strcmp(jo->key, key) == 0) {
			break;
		}
		if(( jo->type == json_object ) || ( jo->type == json_array )) {
			knot=jsonFindKey((jsonObject *)jo->val, key);
			if( knot != NULL ) {
				jo=knot;
				break;
			}
		}
		jo=jo->next;
	}
	return jo;
}

/*
 * fetches all errors from the jsonObject
 */
char *jsonGetError( jsonObject *jo ) {
	char *result=NULL;
	jsonObject *errObj=NULL;

	errObj=jsonFindKey(jo, "jsonError");
	while(errObj != NULL) {
		if( result == NULL ) {
			result=calloc(strlen((char*)errObj->val)+3,1);
		}
		else {
			result=realloc(result,strlen(result)+strlen((char*)errObj->val)+3);
		}
		assert(result!=NULL);
		strcat(result, (char*)errObj->val);
		strcat(result,"\n");
		errObj=jsonFindKey(errObj, "jsonError");
	}
	return result;
}

/*
 * returns the jsonType of the current object
 * can also be used to check if an object exists - i.e. array indices
 */
jsonType jsonPeek( jsonObject *jo, const char *key ) {
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
		return atoi( (char*)(pos->val) );
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

int jsonStrcpy( char *target, jsonObject *jo, const char *key, int len ) {
	jo=jsonFollowPath( jo, key );
	if( jo == NULL ) {
		target[0]=0;
		return 0;
	}
	return jsonDecodeInto( (char*)jo->val, target, len );
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
char **jsonGetStrs( jsonObject *jo, const char *key, int *num ) {
	int i;
	char **vals=NULL;
	char *val;
	jsonObject *pos=NULL;

	pos=jsonFollowPath( jo, key );
	*num=jsonGetLength( jo, NULL );

	if( *num > 0 ) {
		vals=(char**)calloc( *num, sizeof( char * ) );
		assert( vals != NULL );
		for( i=0; i<*num; i++ ) {
			val=(char *)jsonGetByIndex( pos, i );
			if( val == NULL ) {
				vals[i]=NULL;
			}
			else {
				vals[i]=jsonDecode(val);
			}
		}
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
	assert( jo->key != NULL );

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
		assert( jo->val != NULL );
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
	jo->val=strdup(buf);
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

/* returns the number of array elements or child objects
   when key is NULL the current object is checked otherwise the path of key
	 will be followed. If the resulting object does not exist, is no array
	 and neither an object the function returns -1.
*/
int jsonGetLength( jsonObject *jo, char *key ) {
	int len=0;

	if ( key != NULL ) {
			jo=jsonFetch(jo, key);
	}
	if((jo == NULL) ||
			((jo->type != json_array) && (jo->type != json_object))) {
		return -1;
	}

	jo=(jsonObject*)jo->val;
	while(jo != NULL) {
		len++;
		jo=jo->next;
	}

	return len;
}

/*
 * Adds a new element to a jsonArray chain.
 * Must not be called with jo == NULL!
 * returns -1 on empty jsonObject tree
 *          0 on error
 *          1 on success
 */
int jsonAddArrElement( jsonObject *jo, void *val, jsonType type ) {
	char key[20];
	int index=0;

	if( jo == NULL ) {
		return -1;
	}

	while( jo->next != NULL ) {
		jo=jo->next;
	}

	if( jo->type != json_array ) {
		jsonFail( jo, "%s is no Array Object to add to!", jo->key );
		return 0;
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
			jsonFail( jo, "Illegal type id %i!", type );
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
			jsonFail( jo, "Illegal type id %i!", type );
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
	jsonParseObject( json, &jo );
	return jo;
}

static char *sizeCheck( char *json, size_t *len ) {
	char *ret=NULL;
	/* an if *should* be enough */
	while( strlen( json ) > (*len)-JSON_LOWATER ) {
		*len=(*len)+JSON_INCBUFF;
		ret=(char*)realloc( json, *len );
		assert( ret != NULL );
		json=ret;
	}
	return json;
}

/**
 * encodes a value into JSON notation
 * "strval"|numval|{objval}|[arr],[val]
 */
static char *jsonWriteVal( jsonObject *jo, char *json, size_t *len ) {
	json=sizeCheck( json, len );
	switch( jo->type ) {
	case json_string:
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
		jsonFail(jo, "Illegal json_type set on %s", jo->key );
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
 * turns the jsonObject tree into a json string.
 * The original jsonObject remains in case errors have been added
 */
char *jsonToString( jsonObject *jo ) {
	char *json=NULL;
	size_t len=JSON_INCBUFF;
	json=(char*)calloc( len, 1 );
	assert( json != NULL );
	json=jsonWriteObj( jo, json, &len );
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
