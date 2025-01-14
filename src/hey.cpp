// hey
// a small scripting utility
// written by Attila Mezei (amezei@mail.datanet.hu)
// public domain, use it at your own risk
//
// 1.2.1:	compiled for x86, R4 with minor modifications at BPropertyInfo
//
// 1.2.0:	the syntax is extended by Sander Stoks (sander@adamation.com) to contain
//		with name=<value> [and name=<value> [...]]
//		at the end of the command which will add additional data to the scripting message. E.g:
//		hey Becasso create Canvas with name=MyCanvas and size=BRect(100,100,300,300)
//		Also a small interpreter is included.
//
//		Detailed printout of B_PROPERTY_INFO in BMessages. Better than BPropertyInfo::PrintToStream().
//		Also prints usage info for a property if defined.
//
// 1.1.1:	minor change from chrish@qnx.com to return -1 if an error is
//		sent back in the reply message; also added B_COUNT_PROPERTIES support
//
//		The range specifier sent to the target was 1 greater than it should've been. Fixed.
//
//		'hey' made the assumption that the first thread in a team will be the 
//		application thread (and therefore have the application's name).  
//		This was not always the case. Fix from Scott Lindsey <wombat@gobe.com>.
//
//v1.1.0:	Flattened BPropertyInfo is printed if found in the reply of B_GET_SUPPORTED_SUITES 
//		1,2,3 and 4 character message constant is supported (e.g. '1', '12', '123', '1234') 
//		Alpha is sent with rgb_color 
//
//v1.0.0	First public release 

#include "textbuffer.h"
#include "hey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <AppKit.h>
#include <Path.h>
#include "PropDump.h"

extern "C" {
#include "bio.h"
}

const char VERSION[]="v1.2.1";

#define DEBUG_HEY 0		// 1: prints the script message to be sent to the target application, 0: prints only the reply

#ifdef STANDALONE
int main(int argc, char *argv[])
{
	BApplication app("application/x-amezei-hey");

	if (argc < 2) {
		fprintf(stderr, "hey %s, written by Attila Mezei (amezei@mail.datanet.hu)\n" \
		"usage: hey <app|signature> <verb> <specifier_1> <of <specifier_n>>* [to <value>]\n" \
		"           [with name=<value> [and name=<value>]*]\n" \
		"where <verb> : GET|SET|COUNT|CREATE|DELETE|GETSUITES|QUIT|SAVE|LOAD|'what'\n"
		"      <specifier> : <property_name> [ '['index']' | '['-reverse_index']' | \n" \
		"                     '['fromvalue to tovalue']' | name | \"name\" ]\n" \
		"      <value> : \"string\" | <integer> | <float> | bool(value) | int8(value) |\n" \
		"                int16(value) | int32(value) | float(value) | double(value) |\n" \
		"                BPoint(x,y) | BRect(l,t,r,b) | rgb_color(r,g,b,a) | file(path)\n\n", VERSION);
		return -1;
	}
	

	// find the application
	BMessenger the_application;
	BList team_list;
	team_id teamid;
	app_info appinfo;
	
	be_roster->GetAppList(&team_list);
	for(int32 i=0;i<team_list.CountItems();i++){
		teamid=(team_id)team_list.ItemAt(i);
		be_roster->GetRunningAppInfo(teamid, &appinfo);
		if(strcmp(appinfo.signature, argv[1])==0){
			the_application=BMessenger(appinfo.signature);
			break;
		}else{
			if(strcmp(appinfo.ref.name, argv[1])==0){
				the_application=BMessenger(0, teamid);
				break;
			}
		}
	}
	
	if(!the_application.IsValid()){
		fprintf(stderr, "Cannot find the application (%s)\n", argv[1]);
		return -1;
	}

	if (argc < 3) {
		fprintf(stderr, "Cannot find the verb!\n");
		return -1;
	}

	
	BMessage the_reply;
	int32 argx = 2;
//	status_t err = Hey(&the_application, "set File of Window Sample to file(/boot/home/media/images/BeLogo.psd)", &the_reply);
	status_t err = Hey(&the_application, argv, &argx, argc, &the_reply);

	if (err!=B_OK) {
		fprintf(stderr, "Error when sending message to %s!\n", argv[1]);
		return -1;
	} else {
		if(the_reply.what==(uint32)B_MESSAGE_NOT_UNDERSTOOD || the_reply.what==(uint32)B_ERROR){	// I do it myself
			if(the_reply.HasString("message")){
				printf("%s (error 0x%8lX)\n", the_reply.FindString("message"), the_reply.FindInt32("error"));
			}else{
				printf("error 0x%8lX\n", the_reply.FindInt32("error"));
			}
		}else{
			printf("Reply ");
			print_message(&the_reply);
			printf("\n");
		}
	}
	
	return 0;
}

int32 HeyInterpreterThreadHook(void* arg)
{
	if (arg) {
		BMessage environment((BMessage*) arg);
		char* prompt = "Hey";
		if (environment.HasString("prompt")) environment.FindString("prompt", (const char **)&prompt);
		printf("%s> ", prompt);
		
		BMessenger target;
		if (environment.HasMessenger("Target")) environment.FindMessenger("Target", &target);

		char command[1024];
		status_t err;
		BMessage reply;
		while (gets(command)) {
			reply.MakeEmpty();
			err = Hey(&target, command, &reply);
			if (!err) {
				print_message(&reply);
			} else {
				printf("Error!\n");
			}
			printf("%s> ", prompt);
		}
		
		return 0;

	} else {
		return 1;
	}
}
#endif

status_t Hey(BMessenger* target, const char* arg, BMessage* reply)
{
	char* argv[100];
	char* tokens = new char[strlen(arg)*2];
	char* currentToken = tokens;
	int32 tokenNdex = 0;
	int32 argNdex = 0;
	int32 argc = 0;
	bool inquotes = false;

	while (arg[argNdex] != 0) { // for each character in arg
		if (arg[argNdex] == '\"') inquotes = !inquotes;
		if (!inquotes && isSpace(arg[argNdex])) { // if the character is white space
			if (tokenNdex!=0) { //  close off currentToken token
				currentToken[tokenNdex] = 0; 
				argv[argc] = currentToken;
				argc++;
				currentToken += tokenNdex+1;
				tokenNdex=0;
				argNdex++;
			} else { // just skip the whitespace
				argNdex++; 
			}
		} else { // copy char into current token
			currentToken[tokenNdex] = arg[argNdex];
			tokenNdex++;
			argNdex++;
		}
	}
	
	if (tokenNdex!=0) { //  close off currentToken token
		currentToken[tokenNdex] = 0; 
		argv[argc] = currentToken;
		argc++;
	}
	argv[argc] = NULL;
	
	int32 argx = 0;
	return Hey(target, argv, &argx, argc, reply);
	delete tokens;
}

bool isSpace(char c)
{
	switch (c) {
		case ' ':
		case '\t':
			return true;

		default:
			return false;
	}
}

status_t Hey(BMessenger* target, char* argv[], int32* argx, int32 argc, BMessage* reply)
{
	BMessage the_message;
	if(strcasecmp(argv[*argx], "get")==0){
		the_message.what=B_GET_PROPERTY;
	}else if(strcasecmp(argv[*argx], "set")==0){
		the_message.what=B_SET_PROPERTY;
	}else if(strcasecmp(argv[*argx], "execute")==0){
		the_message.what=B_EXECUTE_PROPERTY;
	}else if(strcasecmp(argv[*argx], "create")==0){
		the_message.what=B_CREATE_PROPERTY;
	}else if(strcasecmp(argv[*argx], "delete")==0){
		the_message.what=B_DELETE_PROPERTY;
	}else if(strcasecmp(argv[*argx], "quit")==0){
		the_message.what=B_QUIT_REQUESTED;
	}else if(strcasecmp(argv[*argx], "save")==0){
		the_message.what=B_SAVE_REQUESTED;
	}else if(strcasecmp(argv[*argx], "load")==0){
		the_message.what=B_REFS_RECEIVED;
	}else if(strcasecmp(argv[*argx], "getsuites")==0){
		the_message.what=B_GET_SUPPORTED_SUITES;
	}else if(strcasecmp(argv[*argx], "count")==0){
		the_message.what=B_COUNT_PROPERTIES;
	}else{
		switch(strlen(argv[*argx])){	// can be a message constant if 1,2,3 or 4 chars
			case 1:
						the_message.what=(int32)argv[*argx][0];
						break;
			case 2:
						the_message.what=(((int32)argv[*argx][0])<<8)|(((int32)argv[*argx][1]));
						break;
			case 3:
						the_message.what=(((int32)argv[*argx][0])<<16)|(((int32)argv[*argx][1])<<8)|(((int32)argv[*argx][2]));
						break;
			case 4:			
						the_message.what=(((int32)argv[*argx][0])<<24)|(((int32)argv[*argx][1])<<16)|(((int32)argv[*argx][2])<<8)|(((int32)argv[*argx][3]));
						break;
			default:
						beprintf("Bad verb (\"%s\")\n", argv[*argx]);
						return -1;
		}
	}
	// parse the specifiers
	(*argx)++;
	status_t result=B_OK;
	if(the_message.what!=B_REFS_RECEIVED){	// LOAD has no specifier
		while((result=add_specifier(&the_message, argv, argx, argc))==B_OK){};
	
		if(result!=B_ERROR){	// bad syntax
			beprintf("Bad specifier syntax!\n");
			return result;
		}
	}
	// if verb is SET or LOAD or EXECUTE, there should be a to <value>
	if((the_message.what==B_SET_PROPERTY || the_message.what==B_REFS_RECEIVED) && argv[*argx]!=NULL){
		if(strcasecmp(argv[*argx], "to")==0){
			(*argx)++;
		}
		result=add_data(&the_message, argv, argx);
		if(result!=B_OK){
			if(result==B_FILE_NOT_FOUND){
				beprintf("File not found!\n");
			}else{
				beprintf("Invalid 'to...' value format!\n");
			}
			return result;
		}
	}
	add_with(&the_message, argv, argx, argc);
	
#if DEBUG_HEY>0
	beprintf("Send ");
	print_message(&the_message);
	beprintf("\n");
#endif
	if (target && target->IsValid()) {
		if (reply) {
			result = target->SendMessage(&the_message, reply);
		} else {
			result = target->SendMessage(&the_message);
		}
	}
	return result;
}

// There can be a with <name>=<type>() [and <name>=<type> ...]
// I treat "and" just the same as "with", it's just to make the script syntax more English-like.
status_t add_with(BMessage *to_message, char *argv[], int32 *argx, int32 argc)
{
	status_t result=B_OK;
	if(*argx < argc - 1 && argv[++(*argx)]!=NULL){
		// bprintf ("argv[%d] = %s\n", *argx, argv[*argx]);
		if(strcasecmp(argv[*argx], "with")==0){
			// bprintf ("""with"" detected!\n");
			(*argx)++;
			bool done = false;
			do
			{
				result=add_data(to_message, argv, argx);
				if(result!=B_OK){
					if(result==B_FILE_NOT_FOUND){
						beprintf("File not found!\n");
					}else{
						beprintf("Invalid 'with...' value format!\n");
					}
					return result;
				}
				(*argx)++;
				// bprintf ("argc = %d, argv[%d] = %s\n", argc, *argx, argv[*argx]);
				if (*argx < argc - 1 && strcasecmp(argv[*argx], "and")==0)
				{
					(*argx)++;
				}
				else
					done = true;
			} while (!done);
		}
	}
	return result;
}

// returns B_OK if successful 
//         B_ERROR if no more specifiers
//         B_BAD_SCRIPT_SYNTAX if syntax error
status_t add_specifier(BMessage *to_message, char *argv[], int32 *argx, int32 argc)
{

	char *property=argv[*argx];
	
	if(property==NULL) return B_ERROR;		// no more specifiers
	
	(*argx)++;
	
	if(strcasecmp(property, "to")==0){	// it is the 'to' string!!!
		return B_ERROR;	// no more specifiers
	}
	
	if(strcasecmp(property, "with")==0){	// it is the 'with' string!!!
		*argx -= 2;
		add_with (to_message, argv, argx, argc);
		return B_ERROR;	// no more specifiers
	}
	
	if(strcasecmp(property, "of")==0){		// skip "of", read real property
		property=argv[*argx];
		if(property==NULL) return B_BAD_SCRIPT_SYNTAX;		// bad syntax
		(*argx)++;
	}
	
	// decide the specifier

	char *specifier=argv[*argx];
	if(specifier==NULL){	// direct specifier
		to_message->AddSpecifier(property);
		return B_ERROR;		// no more specifiers
	}

	(*argx)++;

	if(strcasecmp(specifier, "of")==0){	// direct specifier
		to_message->AddSpecifier(property);
		return B_OK;	
	}

	if(strcasecmp(specifier, "to")==0){	// direct specifier
		to_message->AddSpecifier(property);
		return B_ERROR;		// no more specifiers	
	}


	if(specifier[0]=='['){	// index, reverse index or range
		char *end;
		int32 ix1, ix2;
		if(specifier[1]=='-'){	// reverse index
			ix1=strtoul(specifier+2, &end, 10);
			BMessage revspec(B_REVERSE_INDEX_SPECIFIER);
			revspec.AddString("property", property);
			revspec.AddInt32("index", ix1);
			to_message->AddSpecifier(&revspec);
		}else{	// index or range
			ix1=strtoul(specifier+1, &end, 10);
			if(end[0]==']'){	// it was an index
				to_message->AddSpecifier(property, ix1);
				return B_OK;	
			}else{
				specifier=argv[*argx];
				if(specifier==NULL){
					// I was wrong, it was just an index
					to_message->AddSpecifier(property, ix1);
					return B_OK;	
				}		
				(*argx)++;
				if(strcasecmp(specifier, "to")==0){
					specifier=argv[*argx];
					if(specifier==NULL){
						return B_BAD_SCRIPT_SYNTAX;		// wrong syntax
					}
					(*argx)++;
					ix2=strtoul(specifier, &end, 10);
					to_message->AddSpecifier(property, ix1, ix2-ix1>0 ? ix2-ix1 : 1);
					return B_OK;		
				}else{
					return B_BAD_SCRIPT_SYNTAX;		// wrong syntax
				}
			}
		}
	}else{	// name specifier
		// if it contains only digits, it will be an index...
		bool contains_only_digits=true;
		for(int32 i=0;i<(int32)strlen(specifier);i++){
			if(specifier[i]<'0' || specifier[i]>'9'){
				contains_only_digits=false;
				break;
			}
		}
		
		if(contains_only_digits){
			to_message->AddSpecifier(property, atol(specifier));
		}else{
			to_message->AddSpecifier(property, specifier);
		}
		
	}
	
	return B_OK;
}


status_t add_data(BMessage *to_message, char *argv[], int32 *argx)
{
	char *valuestring=argv[*argx];
	
	if(valuestring==NULL) return B_ERROR;
	
	// try to interpret it as an integer or float
	bool contains_only_digits=true;
	bool is_floating_point=false;
	for(int32 i=0;i<(int32)strlen(valuestring);i++){
		if(valuestring[i]<'0' || valuestring[i]>'9'){
			if(valuestring[i]=='.'){
				is_floating_point=true;
			}else{
				contains_only_digits=false;
				break;
			}
		}
	}
	
	if(contains_only_digits){
		if(is_floating_point){
			to_message->AddFloat("data", atof(valuestring));
			return B_OK;
		}else{
			to_message->AddInt32("data", atol(valuestring));
			return B_OK;
		}
	}
	
	// if true or false, it is bool
	if(strcasecmp(valuestring, "true")==0){
		to_message->AddBool("data", true);
		return B_OK;
	}else if(strcasecmp(valuestring, "false")==0){
		to_message->AddBool("data", false);
		return B_OK;
	}

	// Add support for "<name>=<type>()" here:
	// The type is then added under the name "name".

	#define MAX_NAME_LENGTH 128
	char curname[MAX_NAME_LENGTH];
	strcpy (curname, "data");	// This is the default.
	
	char *s = valuestring;
	while (*++s && *s != '=')
		// Look for a '=' character...
		;
	if (*s == '=')	// We found a <name>= 
	{
		*s = 0;
		strcpy (curname, valuestring);	// Use the new <name>
		valuestring = s + 1;			// Reposition the valuestring ptr.
	}

	// must begin with a type( value )
	if(strncasecmp(valuestring, "int8", strlen("int8"))==0){
		to_message->AddInt8(curname, atol(valuestring+strlen("int8(")));
		return B_OK;
	}else if(strncasecmp(valuestring, "int16", strlen("int16"))==0){
		to_message->AddInt16(curname, atol(valuestring+strlen("int16(")));
		return B_OK;
	}else if(strncasecmp(valuestring, "int32", strlen("int32"))==0){
		to_message->AddInt32(curname, atol(valuestring+strlen("int32(")));
		return B_OK;
	}else if(strncasecmp(valuestring, "int64", strlen("int64"))==0){
		to_message->AddInt64(curname, atol(valuestring+strlen("int64(")));
		return B_OK;
	}else if(strncasecmp(valuestring, "bool", strlen("bool"))==0){
		if(strncasecmp(valuestring+strlen("bool("), "true", 4)==0){
			to_message->AddBool(curname, true);
		}else if(strncasecmp(valuestring+strlen("bool("), "false", 5)==0){
			to_message->AddBool(curname, false);
		}else{
			to_message->AddBool(curname, atol(valuestring+strlen("bool("))==0 ? false : true);
		}
		return B_OK;
	}else if(strncasecmp(valuestring, "float", strlen("float"))==0){
		to_message->AddFloat(curname, atof(valuestring+strlen("float(")));
		return B_OK;
	}else if(strncasecmp(valuestring, "double", strlen("double"))==0){
		to_message->AddDouble(curname, atof(valuestring+strlen("double(")));
		return B_OK;
	}else if(strncasecmp(valuestring, "BPoint", strlen("BPoint"))==0){
		float x,y;
		x=atof(valuestring+strlen("BPoint("));
		if(strchr(valuestring, ',')){
			y=atof(strchr(valuestring, ',')+1);
		}else if(strchr(valuestring, ' ')){
			y=atof(strchr(valuestring, ' ')+1);
		}else{	// bad syntax
			y=0.0f;
		}
		to_message->AddPoint(curname, BPoint(x,y));
		return B_OK;
	}else if(strncasecmp(valuestring, "BRect", strlen("BRect"))==0){
		float l=0.0f, t=0.0f, r=0.0f, b=0.0f;
		char *ptr;
		l=atof(valuestring+strlen("BRect("));
		ptr=strchr(valuestring, ',');
		if(ptr){
			t=atof(ptr+1);
			ptr=strchr(ptr+1, ',');
			if(ptr){
				r=atof(ptr+1);
				ptr=strchr(ptr+1, ',');
				if(ptr){
					b=atof(ptr+1);
				}
			}
		}
		
		to_message->AddRect(curname, BRect(l,t,r,b));
		return B_OK;
	}else if(strncasecmp(valuestring, "rgb_color", strlen("rgb_color"))==0){
		rgb_color clr;
		char *ptr;
		clr.red=atol(valuestring+strlen("rgb_color("));
		ptr=strchr(valuestring, ',');
		if(ptr){
			clr.green=atol(ptr+1);
			ptr=strchr(ptr+1, ',');
			if(ptr){
				clr.blue=atol(ptr+1);
				ptr=strchr(ptr+1, ',');
				if(ptr){
					clr.alpha=atol(ptr+1);
				}
			}
		}
		
		to_message->AddData(curname, B_RGB_COLOR_TYPE, &clr, sizeof(rgb_color));
		return B_OK;
	}else if(strncasecmp(valuestring, "file", strlen("file"))==0){
		entry_ref file_ref;
		
		// remove the last ] or )
		if(valuestring[strlen(valuestring)-1]==')' || valuestring[strlen(valuestring)-1]==']'){
			valuestring[strlen(valuestring)-1]=0;
		}
		
		if(get_ref_for_path(valuestring+5, &file_ref)!=B_OK){
			return B_FILE_NOT_FOUND;
		}
		
		// check if the ref is valid
		BEntry entry;
		if(entry.SetTo(&file_ref)!=B_OK) return B_FILE_NOT_FOUND;
		//if(!entry.Exists())  return B_FILE_NOT_FOUND;
		
		// add both ways, refsreceived needs it as "refs" while scripting needs "data"
		to_message->AddRef("refs", &file_ref);
		to_message->AddRef(curname, &file_ref);
		return B_OK;
	}else{	// it is string
		// does it begin with a quote?
		if(valuestring[0]=='\"'){
			if(valuestring[strlen(valuestring)-1]=='\"') valuestring[strlen(valuestring)-1]=0;
			to_message->AddString(curname, valuestring+1);
		}else{
			to_message->AddString(curname, valuestring);
		}
		return B_OK;
	}

	return B_OK;
}



void print_message(BMessage *message)
{
	TextBuffer tb;
	add_message_contents(tb, message, 0);
        bprintf(tb.text());
}

void add_message_contents(TextBuffer &tb, BMessage *msg, int32 level)
{
	int32 count;
	ssize_t i, sizefound, j;
	type_code typefound;
	char *namefound;
	void *voidptr;
	BMessage a_message;
	char *datatype, *content;

	// go though all message data
	count=msg->CountNames(B_ANY_TYPE);
	for(i=0;i<count;i++){
		msg->GetInfo(B_ANY_TYPE, i, &namefound, &typefound);
		j=0;
		
		while(msg->FindData(namefound, typefound, j++, (const void **)&voidptr, &sizefound)==B_OK){
			datatype=get_datatype_string(typefound);
			content=format_data(typefound, (char*)voidptr, sizefound);
			tb.printf("%s %s %s\n", namefound, datatype, content);
			delete [] datatype;
			delete [] content;
			
			if(typefound==B_MESSAGE_TYPE){
				msg->FindMessage(namefound, j-1, &a_message);
				add_message_contents(tb, &a_message, level+1);
			}else
			if(typefound==B_RAW_TYPE && strcmp(namefound, "_previous_")==0){
				if(a_message.Unflatten((const char *)voidptr)==B_OK){
					add_message_contents(tb, &a_message, level+1);
				}
			}
		}
	}
}


extern "C" {
extern void be_set_var(char *, char *);
extern void be_set_cvar(char *, char *, char *);
}

long message_to_result(BMessage *msg, long total)
{
	int32 count;
	ssize_t i, sizefound, j;
	type_code typefound;
	char *namefound;
	void *voidptr;
	BMessage a_message;
	char *datatype, *content;
	// go though all message data
	count=msg->CountNames(B_ANY_TYPE);

	for(i=0;i<count;i++){
		msg->GetInfo(B_ANY_TYPE, i, &namefound, &typefound);
		j=0;
		
		while(msg->FindData(namefound, typefound, j++, (const void **)&voidptr, &sizefound)==B_OK){
			datatype=get_datatype_string(typefound);
			content=format_data(typefound, (char*)voidptr, sizefound);
                        total++;
                        if (total == 1) {
				be_set_var("RESULT", content);
                        	be_set_cvar("RESULT", "NAME", namefound);
                        	be_set_cvar("RESULT", "TYPE", datatype);
                        }
                        char totbuff[10];
                        sprintf(totbuff, "%d", total);
			be_set_cvar("RESULT", totbuff, content);
                        be_set_cvar("RESULT.NAME", totbuff, namefound);
                        be_set_cvar("RESULT.TYPE", totbuff, datatype);
			delete [] datatype;
			delete [] content;
			
			if(typefound==B_MESSAGE_TYPE){
				msg->FindMessage(namefound, j-1, &a_message);
				total = message_to_result(&a_message, total);
			}else
			if(typefound==B_RAW_TYPE && strcmp(namefound, "_previous_")==0){
				if(a_message.Unflatten((const char *)voidptr)==B_OK){
					total = message_to_result(&a_message, total);
				}
			}
		}
	}
	return(total);
}

char *get_datatype_string(int32 type)
{
	char *str=new char[128];
	
	switch(type){
		case B_ANY_TYPE:	strcpy(str, "B_ANY_TYPE"); break;
		case B_ASCII_TYPE:	strcpy(str, "B_ASCII_TYPE"); break;
		case B_BOOL_TYPE:	strcpy(str, "B_BOOL_TYPE"); break;
		case B_CHAR_TYPE:	strcpy(str, "B_CHAR_TYPE"); break;
		case B_COLOR_8_BIT_TYPE:	strcpy(str, "B_COLOR_8_BIT_TYPE"); break;
		case B_DOUBLE_TYPE:	strcpy(str, "B_DOUBLE_TYPE"); break;
		case B_FLOAT_TYPE:	strcpy(str, "B_FLOAT_TYPE"); break;
		case B_GRAYSCALE_8_BIT_TYPE:	strcpy(str, "B_GRAYSCALE_8_BIT_TYPE"); break;
		case B_INT64_TYPE:	strcpy(str, "B_INT64_TYPE"); break;
		case B_INT32_TYPE:	strcpy(str, "B_INT32_TYPE"); break;
		case B_INT16_TYPE:	strcpy(str, "B_INT16_TYPE"); break;
		case B_INT8_TYPE:	strcpy(str, "B_INT8_TYPE"); break;
		case B_MESSAGE_TYPE:	strcpy(str, "B_MESSAGE_TYPE"); break;
		case B_MESSENGER_TYPE:	strcpy(str, "B_MESSENGER_TYPE"); break;
		case B_MIME_TYPE:	strcpy(str, "B_MIME_TYPE"); break;
		case B_MONOCHROME_1_BIT_TYPE:	strcpy(str, "B_MONOCHROME_1_BIT_TYPE"); break;
		case B_OBJECT_TYPE:	strcpy(str, "B_OBJECT_TYPE"); break;
		case B_OFF_T_TYPE:	strcpy(str, "B_OFF_T_TYPE"); break;
		case B_PATTERN_TYPE:	strcpy(str, "B_PATTERN_TYPE"); break;
		case B_POINTER_TYPE:	strcpy(str, "B_POINTER_TYPE"); break;
		case B_POINT_TYPE:	strcpy(str, "B_POINT_TYPE"); break;
		case B_RAW_TYPE:	strcpy(str, "B_RAW_TYPE"); break;
		case B_RECT_TYPE:	strcpy(str, "B_RECT_TYPE"); break;
		case B_REF_TYPE:	strcpy(str, "B_REF_TYPE"); break;
		case B_RGB_32_BIT_TYPE:	strcpy(str, "B_RGB_32_BIT_TYPE"); break;
		case B_RGB_COLOR_TYPE:	strcpy(str, "B_RGB_COLOR_TYPE"); break;
		case B_SIZE_T_TYPE:	strcpy(str, "B_SIZE_T_TYPE"); break;
		case B_SSIZE_T_TYPE	:	strcpy(str, "B_SSIZE_T_TYPE"); break;
		case B_STRING_TYPE:	strcpy(str, "B_STRING_TYPE"); break;
		case B_TIME_TYPE :	strcpy(str, "B_TIME_TYPE"); break;
		case B_UINT64_TYPE	:	strcpy(str, "B_UINT64_TYPE"); break;
		case B_UINT32_TYPE:	strcpy(str, "B_UINT32_TYPE"); break;
		case B_UINT16_TYPE :	strcpy(str, "B_UINT16_TYPE"); break;
		case B_UINT8_TYPE :	strcpy(str, "B_UINT8_TYPE"); break;
		case B_PROPERTY_INFO_TYPE: strcpy(str, "B_PROPERTY_INFO_TYPE"); break;
		// message constants:
		case B_ABOUT_REQUESTED :	strcpy(str, "B_ABOUT_REQUESTED"); break;
		case B_WINDOW_ACTIVATED :	strcpy(str, "B_WINDOW_ACTIVATED"); break;
		case B_ARGV_RECEIVED :	strcpy(str, "B_ARGV_RECEIVED"); break;
		case B_QUIT_REQUESTED :	strcpy(str, "B_QUIT_REQUESTED"); break;
		case B_CANCEL :	strcpy(str, "B_CANCEL"); break;
		case B_KEY_DOWN :	strcpy(str, "B_KEY_DOWN"); break;
		case B_KEY_UP :	strcpy(str, "B_KEY_UP"); break;
		case B_MINIMIZE	 :	strcpy(str, "B_MINIMIZE"); break;
		case B_MOUSE_DOWN :	strcpy(str, "B_MOUSE_DOWN"); break;
		case B_MOUSE_MOVED :	strcpy(str, "B_MOUSE_MOVED"); break;
		case B_MOUSE_ENTER_EXIT	 :	strcpy(str, "B_MOUSE_ENTER_EXIT"); break;
		case B_MOUSE_UP  :	strcpy(str, "B_MOUSE_UP"); break;
		case B_PULSE  :	strcpy(str, "B_PULSE"); break;
		case B_READY_TO_RUN :	strcpy(str, "B_READY_TO_RUN"); break;
		case B_REFS_RECEIVED :	strcpy(str, "B_REFS_RECEIVED"); break;
		case B_SCREEN_CHANGED :	strcpy(str, "B_SCREEN_CHANGED"); break;
		case B_VALUE_CHANGED :	strcpy(str, "B_VALUE_CHANGED"); break;
		case B_VIEW_MOVED :	strcpy(str, "B_VIEW_MOVED"); break;
		case B_VIEW_RESIZED :	strcpy(str, "B_VIEW_RESIZED"); break;
		case B_WINDOW_MOVED :	strcpy(str, "B_WINDOW_MOVED"); break;
		case B_WINDOW_RESIZED :	strcpy(str, "B_WINDOW_RESIZED"); break;
		case B_WORKSPACES_CHANGED :	strcpy(str, "B_WORKSPACES_CHANGED"); break;
		case B_WORKSPACE_ACTIVATED :	strcpy(str, "B_WORKSPACE_ACTIVATED"); break;
		case B_ZOOM	 :	strcpy(str, "B_ZOOM"); break;
		case _APP_MENU_ :	strcpy(str, "_APP_MENU_"); break;
		case _BROWSER_MENUS_ :	strcpy(str, "_BROWSER_MENUS_"); break;
		case _MENU_EVENT_  :	strcpy(str, "_MENU_EVENT_"); break;
		case _QUIT_  :	strcpy(str, "_QUIT_"); break;
		case _VOLUME_MOUNTED_  :	strcpy(str, "_VOLUME_MOUNTED_"); break;
		case _VOLUME_UNMOUNTED_	 :	strcpy(str, "_VOLUME_UNMOUNTED_"); break;
		case _MESSAGE_DROPPED_ 	 :	strcpy(str, "_MESSAGE_DROPPED_"); break;
		case _MENUS_DONE_ :	strcpy(str, "_MENUS_DONE_"); break;
		case _SHOW_DRAG_HANDLES_	 :	strcpy(str, "_SHOW_DRAG_HANDLES_"); break;
		case B_SET_PROPERTY		 :	strcpy(str, "B_SET_PROPERTY"); break;
		case B_GET_PROPERTY	 :	strcpy(str, "B_GET_PROPERTY"); break;
                case B_EXECUTE_PROPERTY  :      strcpy(str, "B_EXECUTE_PROPERTY"); break;
		case B_CREATE_PROPERTY	 :	strcpy(str, "B_CREATE_PROPERTY"); break;
		case B_DELETE_PROPERTY	 :	strcpy(str, "B_DELETE_PROPERTY"); break;
		case B_COUNT_PROPERTIES	 :	strcpy(str, "B_COUNT_PROPERTIES"); break;
		case B_GET_SUPPORTED_SUITES	 :	strcpy(str, "B_GET_SUPPORTED_SUITES"); break;
		case B_CUT 	 :	strcpy(str, "B_CUT"); break;
		case B_COPY 	 :	strcpy(str, "B_COPY"); break;
		case B_PASTE 	 :	strcpy(str, "B_PASTE"); break;
		case B_SELECT_ALL	 :	strcpy(str, "B_SELECT_ALL"); break;
		case B_SAVE_REQUESTED 	 :	strcpy(str, "B_SAVE_REQUESTED"); break;
		case B_MESSAGE_NOT_UNDERSTOOD :	strcpy(str, "B_MESSAGE_NOT_UNDERSTOOD"); break;
		case B_NO_REPLY 	 :	strcpy(str, "B_NO_REPLY"); break;
		case B_REPLY  :	strcpy(str, "B_REPLY"); break;
		case B_SIMPLE_DATA	 :	strcpy(str, "B_SIMPLE_DATA"); break;
		//case B_MIME_DATA	 :	strcpy(str, "B_MIME_DATA"); break;
		case B_ARCHIVED_OBJECT	 :	strcpy(str, "B_ARCHIVED_OBJECT"); break;
		case B_UPDATE_STATUS_BAR :	strcpy(str, "B_UPDATE_STATUS_BAR"); break;
		case B_RESET_STATUS_BAR	 :	strcpy(str, "B_RESET_STATUS_BAR"); break;
		case B_NODE_MONITOR		 :	strcpy(str, "B_NODE_MONITOR"); break;
		case B_QUERY_UPDATE	 :	strcpy(str, "B_QUERY_UPDATE"); break;
		case B_BAD_SCRIPT_SYNTAX: strcpy(str, "B_BAD_SCRIPT_SYNTAX"); break;

		// specifiers:
		case B_NO_SPECIFIER	 :	strcpy(str, "B_NO_SPECIFIER"); break;
		case B_DIRECT_SPECIFIER	 :	strcpy(str, "B_DIRECT_SPECIFIER"); break;
		case B_INDEX_SPECIFIER	 :	strcpy(str, "B_INDEX_SPECIFIER"); break;
		case B_REVERSE_INDEX_SPECIFIER	 :	strcpy(str, "B_REVERSE_INDEX_SPECIFIER"); break;
		case B_RANGE_SPECIFIER	 :	strcpy(str, "B_RANGE_SPECIFIER"); break;
		case B_REVERSE_RANGE_SPECIFIER	 :	strcpy(str, "B_REVERSE_RANGE_SPECIFIER"); break;
		case B_NAME_SPECIFIER	 :	strcpy(str, "B_NAME_SPECIFIER"); break;

		case B_ERROR	 :	strcpy(str, "B_ERROR"); break;
				
		default:	// unknown
					id_to_string(type, str);
					break;
	}
	
	return str;

}


char *format_data(int32 type, char *ptr, long size)
{
	char idtext[32];
	char *str;
	float *fptr;
	double *dptr;
//	BRect *brptr;
	long i;
	entry_ref aref;
	BEntry entry;
	BPath path;
	int64 i64;
	int32 i32;
	int16 i16;
	int8 i8;
	uint64 ui64;
	uint32 ui32;
	uint16 ui16;
	uint8 ui8;
	BMessage anothermsg;
	BPropertyInfo propinfo;
	const property_info *pinfo;
	int32 pinfo_index;
	char *tempstr;
	
	
	if(size<=0L){
		str=new char;
		*str=0;
		return str;
	}

	switch(type){
		case B_MIME_TYPE:
		case B_ASCII_TYPE:
		case B_STRING_TYPE:
					if(size>512) size=512;
					str=new char[size+4];
					strncpy(str, ptr, size);
					break;
		case B_POINTER_TYPE:
					str=new char[64];
					sprintf(str, "%p", *(void**)ptr);
					break;

		case B_REF_TYPE:
					str=new char[1024];
					anothermsg.AddData("myref", B_REF_TYPE, ptr, size);
					anothermsg.FindRef("myref", &aref);
					if(entry.SetTo(&aref)==B_OK){
						entry.GetPath(&path);
						strcpy(str, path.Path());
					}else{
						strcpy(str, "invalid entry_ref");
					}
					break;
					
		case B_SSIZE_T_TYPE:
		case B_INT64_TYPE:
					str=new char[64];
					i64=*(int64*)ptr;
					sprintf(str, "%Ld", i64);
					break;
		
		case B_SIZE_T_TYPE:
		case B_INT32_TYPE:
					str=new char[64];
					i32=*(int32*)ptr;
					sprintf(str, "%ld", i32);
					break;
		
		case B_INT16_TYPE:
					str=new char[64];
					i16=*(int16*)ptr;
					sprintf(str, "%d", i16);
					break;
		
		case B_CHAR_TYPE:
		case B_INT8_TYPE:
					str=new char[64];
					i8=*(int8*)ptr;
					sprintf(str, "%d", i8);
					break;
		
		case B_UINT64_TYPE:
					str=new char[64];
					ui64=*(uint64*)ptr;
					sprintf(str, "%Lu", ui64);
					break;
		
		case B_UINT32_TYPE:
					str=new char[64];
					ui32=*(uint32*)ptr;
					sprintf(str, "%lu", ui32);
					break;
		
		case B_UINT16_TYPE:
					str=new char[64];
					ui16=*(uint16*)ptr;
					sprintf(str, "%u", ui16);
					break;
		
		case B_UINT8_TYPE:
					str=new char[64];
					ui8=*(uint8*)ptr;
					sprintf(str, "%u", ui8);
					break;
		
		case B_BOOL_TYPE:
					str=new char[10];
					if(*ptr){
						strcpy(str, "1");
					}else{
						strcpy(str, "0");
					}
					break;
					
		case B_FLOAT_TYPE:
					str=new char[40];
					fptr=(float*)ptr;
					sprintf(str, "%.3f", *fptr);
					break;
					
		case B_DOUBLE_TYPE:
					str=new char[40];
					dptr=(double*)ptr;
					sprintf(str, "%.3f", *dptr);
					break;
					
		case B_RECT_TYPE:
					str=new char[200];
					fptr=(float*)ptr;
					sprintf(str, "BRect(%.1f, %.1f, %.1f, %.1f)", fptr[0], fptr[1], fptr[2], fptr[3]);
					break;
					
		case B_POINT_TYPE:
					str=new char[200];
					fptr=(float*)ptr;
					sprintf(str, "BPoint(%.1f, %.1f)", fptr[0], fptr[1]);
					break;

		case B_RGB_COLOR_TYPE:	
					str=new char[64];
					sprintf(str, "rgb_color(%u, %u, %u, %u)", ((uint8*)ptr)[0], ((uint8*)ptr)[1], ((uint8*)ptr)[2], ((uint8*)ptr)[3] );
					break;
					
		case B_COLOR_8_BIT_TYPE:
					str=new char[size*6+4];
					*str=0;
					for(i=0;i<min_c(256,size);i++){
						sprintf(idtext, "%u ", ((unsigned char*)ptr)[i]);
						strcat(str,idtext);
					}
					*(str+strlen(str)-2)=0;
					break;

		case B_MESSAGE_TYPE:
					str=new char[64];
					if(anothermsg.Unflatten((const char *)ptr)==B_OK){
						sprintf(str, "what=%s", get_datatype_string(anothermsg.what));
					}else{
						strcpy(str, "error when unflattening");
					}
					break;
					
		case B_PROPERTY_INFO_TYPE:
					if(propinfo.Unflatten(B_PROPERTY_INFO_TYPE, (const void *)ptr, size)==B_OK){
						str=new char[size*8];	// an approximation

						//propinfo.PrintToStream();
						//sprintf(str, "see the printout above");

						pinfo=propinfo.Properties();
						pinfo_index=0;

						sprintf(str, "\n        property   commands                            specifiers\n--------------------------------------------------------------------------------\n");
						
						while(pinfo_index<propinfo.CountProperties()){
						
							strcat(str,  "                "+(strlen(pinfo[pinfo_index].name) <16 ? strlen(pinfo[pinfo_index].name) : 16 ));
							strcat(str, pinfo[pinfo_index].name);
							strcat(str, "   ");
							char *start=str+strlen(str);

							for(int32 i=0;i<10 && pinfo[pinfo_index].commands[i];i++){
								//id_to_string(pinfo[pinfo_index].commands[i], str+strlen(str) );
								tempstr=get_datatype_string(pinfo[pinfo_index].commands[i]);
								strcat(str, tempstr);
								strcat(str, " ");
								delete [] tempstr;
							}
							
							// pad the rest with spaces
							if(strlen(start)<36){
								strcat(str,  "                                    "+strlen(start) );
							}else{
								strcat(str,  "  " );
							}

							for(int32 i=0;i<10 && pinfo[pinfo_index].specifiers[i];i++){
								switch(pinfo[pinfo_index].specifiers[i]){
									case B_NO_SPECIFIER: strcat(str, "NONE "); break;
									case B_DIRECT_SPECIFIER: strcat(str, "DIRECT "); break;
									case B_INDEX_SPECIFIER: strcat(str, "INDEX "); break;
									case B_REVERSE_INDEX_SPECIFIER: strcat(str, "REV.INDEX "); break;
									case B_RANGE_SPECIFIER: strcat(str, "RANGE "); break;
									case B_REVERSE_RANGE_SPECIFIER: strcat(str, "REV.RANGE "); break;
									case B_NAME_SPECIFIER: strcat(str, "NAME"); break;
									case B_ID_SPECIFIER: strcat(str, "ID"); break;
									default: strcat(str, "<NONE> "); break;
								}
							}
							strcat(str, "\n");
							
							// is there usage info?
							if(pinfo[pinfo_index].usage){
								strcat(str, "                   Usage: ");
								strcat(str, pinfo[pinfo_index].usage);
								strcat(str, "\n");
							}

						
							pinfo_index++;		// take next propertyinfo
						}
					}else{
						str=new char[64];
						strcpy(str, "error when unflattening");
					}
					break;

		default:
					str=new char[min_c(256,size)*20+4];
					*str=0;
					for(i=0;i<min_c(256,size);i++){
						//sprintf(idtext, "0x%02X ('%c'), ", (uint16)ptr[i], ptr[i]<32 ? 32 : ptr[i]);
						sprintf(idtext, "0x%02X, ", (uint16)ptr[i] );
						strcat(str,idtext);
					}
					*(str+strlen(str)-2)=0;
					break;
	}
	
	return str;
	
}



char *id_to_string(long ID, char *here)
{
	uint8 digit0=(ID>>24)&255;
	uint8 digit1=(ID>>16)&255;
	uint8 digit2=(ID>>8)&255;
	uint8 digit3=(ID)&255;
	bool itsvalid=false;
	
	if(digit0==0){
		if(digit1==0){
			if(digit2==0){
				// 1 digits
				if(is_valid_char(digit3) ) itsvalid=TRUE;
				sprintf(here, "'%c'", digit3);
			}else{
				// 2 digits
				if(is_valid_char(digit2) && is_valid_char(digit3) ) itsvalid=TRUE;
				sprintf(here, "'%c%c'", digit2, digit3);
			}
		}else{
			// 3 digits
			if(is_valid_char(digit1) && is_valid_char(digit2) && is_valid_char(digit3) ) itsvalid=TRUE;
			sprintf(here, "'%c%c%c'", digit1, digit2, digit3);
		}
	}else{
		// 4 digits
		if(is_valid_char(digit0) && is_valid_char(digit1) && is_valid_char(digit2) && is_valid_char(digit3) ) itsvalid=TRUE;
		sprintf(here, "'%c%c%c%c'", digit0, digit1, digit2, digit3);
	}
	
	if(!itsvalid){
		sprintf(here, "%ldL", ID);
	}

	return here;
}


bool is_valid_char(uint8 c)
{
	return (c>=32 && c<128);
}

