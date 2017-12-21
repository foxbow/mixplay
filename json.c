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


static size_t jsonWriteObj( jsonObject *jo, char *json );
static size_t jsonWriteArr( jsonObject *jo, char *json );
static int jsonFetchObject( char *json, jsonObject **jo );

static int jsonFail( const char *func, char c, const int i, const int state ) {
	addMessage( 0, "%s#%i: Found invalid '%c' in JSON pos %i", func, state, c, i );
	return -1;
}

/**
 * creates an empty json node
 */
static jsonObject *jsonInit() {
	jsonObject *jo;
	jo=falloc(1, sizeof(jsonObject));
	jo->key=NULL;
	jo->next=NULL;
	jo->val=NULL;
	jo->type=0;
	return jo;
}

/*
 * we allow leading zeroes, even if JSON forbids that
 */
static int jsonFetchNum( char *json, char **val ) {
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
					return jsonFail( __func__, json[i], i, state );
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
				return jsonFail( __func__, json[i], i, state );
			}
			break;
		}
		i++;
	}

	return -1;
}

static int jsonFetchString( char *json, char **val ) {
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
				return jsonFail( __func__, json[i], i, state );
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
				return jsonFail( __func__, json[i], i, state );
			}
			break;
		}
		i++;
	}

	return -1;
}

static int jsonFetchKeyVal( char *json, jsonObject **jo ) {
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
				jpos+=jsonFetchString( &json[jpos], &((*jo)->key) );
				state=1;
				break;
			default:
				return jsonFail( __func__, json[jpos], jpos, state );
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
				return jsonFail( __func__, json[jpos], jpos, state );
			}
			break;
		case 2: /* get value */
			switch( json[jpos] ) {
			case ' ':
				jpos++;
				break;
			case '"':
				(*jo)->type=json_string;
				jpos+=jsonFetchString( &json[jpos], (char **)&((*jo)->val) );
				return jpos;
				break;
			case '{':
				(*jo)->type=json_object;
				(*jo)->val=jsonInit();
				jpos+=jsonFetchObject( &json[jpos], (jsonObject **)&((*jo)->val) );
				return jpos;
				break;
			case '[':
				(*jo)->type=json_array;
				fail( F_FAIL, "JSON Arrays are not yet supported.." );
				return jpos;
				break;
			default:
				if( isdigit( json[jpos] ) || json[jpos]=='-' ) {
					(*jo)->type=json_number;
					jpos+=jsonFetchNum( &json[jpos], (char **)&((*jo)->val) );
					return jpos;
				}
				else {
					jsonFail( __func__, json[jpos], jpos, state );
				}
			}
		}
	}

	return -1;
}

static int jsonFetchObject( char *json, jsonObject **jo ) {
	int jpos=0;
	int state=0;
	int len=strlen(json);
	jsonObject **current=jo;

	while( jpos < len ) {
		switch( state ) {
		case 0: /* fetch first value */
			switch( json[jpos] ) {
			case '{':
				jpos++;
				jpos+=jsonFetchKeyVal( json+jpos, current );
				state=1;
				break;
			case ' ':
				jpos++;
				break;
			default:
				return jsonFail( __func__, json[jpos], jpos, state );
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
				jpos+=jsonFetchKeyVal(json+jpos, &((*current)->next));
				current=&((*current)->next);
				break;
			default:
				return jsonFail( __func__, json[jpos], jpos, state );
			}
			break;
		}
	}

	return jpos;
}

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

int jsonGetInt( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_number ) ) {
		return atoi( pos->val );
	}

	return 0;
}

const char *jsonGetStr( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_string ) ) {
		return pos->val;
	}

	addMessage( 1, "No string value for key %s", key );
	return "";
}

int jsonCopyStr( jsonObject *jo, const char *key, char *buf ) {
	strcpy( buf, jsonGetStr(jo, key) );
	return strlen(buf);
}

static void *jsonFetchIndex( jsonObject *jo, int i ) {
	char buf[20];
	jsonObject *val=NULL;

	sprintf( buf, "%i", i );
	val=jsonFollowPath( jo, buf );
	if( val != NULL ) {
		return val->val;
	}

	return NULL;
}

/**
 * copy the array of strings into the vals pointer
 */
int jsonCopyStrs( jsonObject *jo, const char *key, char ***vals, const int num ) {
	int i;
	char *val;
	jsonObject *pos=NULL;

	if( *vals != NULL ) {
		fail( F_FAIL, "array target for %s is not empty!", key );
	}

	pos=jsonFollowPath( jo, key );
	if( (pos != NULL ) && ( pos->type == json_array ) ) {
		*vals=falloc( num, sizeof( char * ) );

		for( i=0; i<num; i++ ) {
			val=(char *)jsonFetchIndex( jo, i );
			(*vals)[i]=falloc( strlen(val)+1, sizeof( char ) );
			strcpy( (*vals)[i], val );
		}

		return num;
	}
	addMessage( 1, "No array value for %s", key );
	return -1;
}

jsonObject *jsonGetObj( jsonObject *jo, const char *key ) {
	jsonObject *pos=jo;

	pos=jsonFollowPath( jo, key );
	if( ( pos != NULL ) && ( pos->type == json_object ) ) {
		return pos->val;
	}

	return NULL;
}

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

	jo->key=falloc( strlen(key)+1, sizeof(char) );
	strcpy( jo->key, key );

	return jo;
}

jsonObject *jsonAddStr( jsonObject *jo, const char *key, const char *val ) {
	jo=jsonAppend( jo, key );
	jo->val=falloc( strlen(val)+1, sizeof(char) );
	jo->type=json_string;
	strcpy( jo->val, val );
	return jo;
}

jsonObject *jsonAddArr( jsonObject *jo, const char *key, jsonObject *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_array;
	jo->val=val;
	return jo;
}

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

jsonObject *jsonAddInt( jsonObject *jo, const char *key, const int val ) {
	char buf[64];
	jo=jsonAppend( jo, key );
	sprintf( buf, "%i", val );
	jo->val=falloc( strlen(buf)+1, sizeof(char) );
	strcpy( jo->val, buf );
	jo->type=json_number;
	return jo;
}

jsonObject *jsonAddObj( jsonObject *jo, const char *key, jsonObject *val ) {
	jo=jsonAppend( jo, key );
	jo->type=json_object;
	jo->val=val;
	return jo;
}

jsonObject *jsonParse( char *json ) {
	jsonObject *jo=NULL;
	if( jsonFetchObject( json, &jo ) >= 0 ){
		return jo;
	}
	else {
		return NULL;
	}
}

static size_t jsonWriteVal( jsonObject *jo, char *json ) {
	switch( jo->type ) {
	case json_string:
		strcat( json, "\"" );
		strcat( json, jo->val );
		strcat( json, "\"" );
		break;
	case json_number:
		strcat( json, jo->val );
		break;
	case json_object:
		jsonWriteObj( jo->val, json );
		break;
	case json_array:
		jsonWriteArr( jo->val, json );
		break;
	case json_none:
		return -1;
		break;
	}
	return strlen( json );
}

static size_t jsonWriteKeyVal( jsonObject *jo, char *json ) {
	strcat( json, "\"" );
	strcat( json, jo->key );
	strcat( json, "\":" );

	jsonWriteVal( jo, json );

	if( jo->next != NULL ) {
		strcat( json, "," );
		jsonWriteKeyVal( jo->next, json );
	}

	return strlen( json );
}

static size_t jsonWriteArr( jsonObject *jo, char *json ) {
	strcat( json, "[" );
	while( jo != NULL ) {
		jsonWriteVal( jo, json );
		jo=jo->next;
		if( jo != NULL ) {
			strcat( json, "," );
		}
	}
	strcat( json, "]" );
	return strlen( json );
}

static size_t jsonWriteObj( jsonObject *jo, char *json ) {
	strcat( json, "{" );
	jsonWriteKeyVal( jo, json );
	strcat( json, "}" );
	return strlen( json );
}

size_t jsonWrite( jsonObject *jo, char *json ) {
	json[0]=0;
	return jsonWriteObj( jo, json );
}

void jsonDiscard( jsonObject *jo, int all ) {
	jsonObject *pos=jo;

	while( jo != NULL ) {
		if( jo->type == json_object ) {
			jsonDiscard( pos->val, all );
			pos->val=NULL;
		}
		pos=jo->next;
		if( all ) {
			free( jo->key );
			if( jo->val != NULL ) {
				free( jo->val );
			}
		}
		free( jo );
		jo=pos;
	}
}
