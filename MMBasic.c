/***********************************************************************************************************************
PicoMite MMBasic

MMBasic.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/

#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include "MMBasic.h"
#include "pico/stdlib.h"

#include "Functions.h"
#include "Commands.h"
#include "Operators.h"
#include "Custom.h"
#include "Hardware_Includes.h"
// this is the command table that defines the various tokens for commands in the source code
// most of them are listed in the .h files so you should not add your own here but instead add
// them to the appropiate .h file
#define INCLUDE_COMMAND_TABLE
const struct s_tokentbl commandtbl[] = {
    #include "Functions.h"
    #include "Commands.h"
    #include "Operators.h"
    #include "Custom.h"
    #include "Hardware_Includes.h"
    { (unsigned char *)"",   0,                  0, cmd_null,    }                   // this dummy entry is always at the end
};
#undef INCLUDE_COMMAND_TABLE



// this is the token table that defines the other tokens in the source code
// most of them are listed in the .h files so you should not add your own here
// but instead add them to the appropiate .h file
#define INCLUDE_TOKEN_TABLE
const struct s_tokentbl tokentbl[] = {
    #include "Functions.h"
    #include "Commands.h"
    #include "Operators.h"
    #include "Custom.h"
    #include "Hardware_Includes.h"
    { (unsigned char *)"",   0,                  0, cmd_null,    }                   // this dummy entry is always at the end
};
#undef INCLUDE_TOKEN_TABLE
struct s_vartbl {                               // structure of the variable table
	unsigned char name[MAXVARLEN];                       // variable's name
	unsigned char type;                                  // its type (T_NUM, T_INT or T_STR)
	unsigned char level;                                 // its subroutine or function level (used to track local variables)
    unsigned char size;                         // the number of chars to allocate for each element in a string array
    unsigned char dummy;
    int __attribute__ ((aligned (4))) dims[MAXDIM];                     // the dimensions. it is an array if the first dimension is NOT zero
    union u_val{
        MMFLOAT f;                              // the value if it is a float
        long long int i;                        // the value if it is an integer
        MMFLOAT *fa;                            // pointer to the allocated memory if it is an array of floats
        long long int *ia;                      // pointer to the allocated memory if it is an array of integers
        unsigned char *s;                                // pointer to the allocated memory if it is a string
    }  __attribute__ ((aligned (8))) val;
} __attribute__ ((aligned (8))) s_vartbl_val;
struct s_hash {                             // structure of the token table
	short hash;                                 // the string (eg, PRINT, FOR, ASC(, etc)
    short level;                                  // the type returned (T_NBR, T_STR, T_INT)
};

// these are initialised at startup
int CommandTableSize, TokenTableSize;

struct s_vartbl *vartbl=NULL;                                            // this table stores all variables
int varcnt;                                                         // number of variables
int VarIndex;                                                       // Global set by findvar after a variable has been created or found
int Localvarcnt;                                                         // number of LOCAL variables
int Globalvarcnt;                                                         // number of GLOBAL variables
int LocalIndex;                                                     // used to track the level of local variables
unsigned char OptionExplicit;                                                // used to force the declaration of variables before their use
unsigned char DefaultType;                                                   // the default type if a variable is not specifically typed
int emptyarray=0;
int TempStringClearStart;                                           // used to prevent clearing of space in an expression that called a FUNCTION

unsigned char *subfun[MAXSUBFUN];                                            // table used to locate all subroutines and functions
char CurrentSubFunName[MAXVARLEN + 1];                              // the name of the current sub or fun
char CurrentInterruptName[MAXVARLEN + 1];                           // the name of the current interrupt function

jmp_buf mark;                                                       // longjump to recover from an error and abort
jmp_buf ErrNext;                                                    // longjump to recover from an error and continue
unsigned char inpbuf[STRINGSIZE];                                            // used to store user keystrokes until we have a line
unsigned char tknbuf[STRINGSIZE];                                            // used to store the tokenised representation of the users input line
//unsigned char lastcmd[STRINGSIZE];                                           // used to store the last command in case it is needed by the EDIT command
unsigned char PromptString[MAXPROMPTLEN];                                    // the prompt for input, an empty string means use the default
int ProgramChanged;                                                 // true if the program in memory has been changed and not saved
struct s_hash hashlist[MAXVARS/2]={0};
int hashlistpointer=0;

unsigned char *ProgMemory;                                                      // program memory, this is where the program is stored
int PSize;                                                          // the size of the program stored in ProgMemory[]

int NextData;                                                       // used to track the next item to read in DATA & READ stmts
unsigned char *NextDataLine;                                                 // used to track the next line to read in DATA & READ stmts
int OptionBase;                                                     // track the state of OPTION BASE
int autoOn, autoNext = 10, autoIncr = 10;                           // use by the AUTO command
//#if defined(MMFAMILY) || defined(DOS)
//unsigned char *ModuleTable[MAXMODULES];                                      // list of pointers to libraries a(also called modules) loaded in memory;
//int NbrModules;                                                     // the number of libraries/modules currently loaded
//#endif
    int  PrepareProgramExt(unsigned char *, int, unsigned char **, int);

#if defined(MMFAMILY)
unsigned char FunKey[NBRPROGKEYS][MAXKEYLEN + 1];                            // data storage for the programmable function keys
#endif
//extern unsigned char MMHeap[Option.HEAP_SIZE];


///////////////////////////////////////////////////////////////////////////////////////////////
// Global information used by operators and functions
//
int targ;                                                           // the type of the returned value
MMFLOAT farg1, farg2, fret;                                           // the two float arguments and returned value
long long int  iarg1, iarg2, iret;                                   // the two integer arguments and returned value
unsigned char *sarg1, *sarg2, *sret;                                         // the two string arguments and returned value

////////////////////////////////////////////////////////////////////////////////////////////////
// Global information used by functions
// functions use targ, fret and sret as defined for operators (above)
unsigned char *ep;                                                           // pointer to the argument to the function terminated with a zero byte.
                                                                    // it is NOT trimmed of spaces

////////////////////////////////////////////////////////////////////////////////////////////////
// Global information used by commands
//
int cmdtoken;                                                       // Token number of the command
unsigned char *cmdline;                                                      // Command line terminated with a zero unsigned char and trimmed of spaces
unsigned char *nextstmt;                                                     // Pointer to the next statement to be executed.
unsigned char *CurrentLinePtr;                                               // Pointer to the current line (used in error reporting)
unsigned char *ContinuePoint;                                                // Where to continue from if using the continue statement

extern int TraceOn;
extern unsigned char *TraceBuff[TRACE_BUFF_SIZE];
extern int TraceBuffIndex;                                          // used for listing the contents of the trace buffer
extern long long int CallCFunction(unsigned char *CmdPtr, unsigned char *ArgList, unsigned char *DefP, unsigned char *CallersLinePtr);

/////////////////////////////////////////////////////////////////////////////////////////////////
// Functions only used within MMBasic.c
//
//void getexpr(unsigned char *);
//void checktype(int *, int);
unsigned char *getvalue(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *oo, int *ta);

unsigned char tokenTHEN, tokenELSE, tokenGOTO, tokenEQUAL, tokenTO, tokenSTEP, tokenWHILE, tokenUNTIL, tokenGOSUB, tokenAS, tokenFOR;
unsigned char cmdIF, cmdENDIF, cmdELSEIF, cmdELSE, cmdSELECT_CASE, cmdFOR, cmdNEXT, cmdWHILE, cmdENDSUB, cmdENDFUNCTION, cmdLOCAL, cmdSTATIC, cmdCASE, cmdDO, cmdLOOP, cmdCASE_ELSE, cmdEND_SELECT;
unsigned char cmdSUB, cmdFUN, cmdCFUN, cmdCSUB, cmdIRET;

/********************************************************************************************************************************************
 Program management
 Includes the routines to initialise MMBasic, start running the interpreter, and to run a program in memory
*********************************************************************************************************************************************/


// Initialise MMBasic
void  InitBasic(void) {
    DefaultType = T_NBR;
    CommandTableSize =  (sizeof(commandtbl)/sizeof(struct s_tokentbl));
    TokenTableSize =  (sizeof(tokentbl)/sizeof(struct s_tokentbl));

    ClearProgram();

    // load the commonly used tokens
    // by looking them up once here performance is improved considerably
    tokenTHEN  = GetTokenValue( (unsigned char *)"Then");
    tokenELSE  = GetTokenValue( (unsigned char *)"Else");
    tokenGOTO  = GetTokenValue( (unsigned char *)"GoTo");
    tokenEQUAL = GetTokenValue( (unsigned char *)"=");
    tokenTO    = GetTokenValue( (unsigned char *)"To");
    tokenSTEP  = GetTokenValue( (unsigned char *)"Step");
    tokenWHILE = GetTokenValue( (unsigned char *)"While");
    tokenUNTIL = GetTokenValue( (unsigned char *)"Until");
    tokenGOSUB = GetTokenValue( (unsigned char *)"GoSub");
    tokenAS    = GetTokenValue( (unsigned char *)"As");
    tokenFOR   = GetTokenValue( (unsigned char *)"For");
    cmdLOOP  = GetCommandValue( (unsigned char *)"Loop");
    cmdIF      = GetCommandValue( (unsigned char *)"If");
    cmdENDIF   = GetCommandValue( (unsigned char *)"EndIf");
//    cmdEND_IF  = GetCommandValue( (unsigned char *)"End If");
    cmdELSEIF  = GetCommandValue( (unsigned char *)"ElseIf");
//    cmdELSE_IF = GetCommandValue( (unsigned char *)"Else If");
    cmdELSE    = GetCommandValue( (unsigned char *)"Else");
    cmdSELECT_CASE = GetCommandValue( (unsigned char *)"Select Case");
    cmdCASE        = GetCommandValue( (unsigned char *)"Case");
    cmdCASE_ELSE   = GetCommandValue( (unsigned char *)"Case Else");
    cmdEND_SELECT  = GetCommandValue( (unsigned char *)"End Select");
	cmdSUB = GetCommandValue( (unsigned char *)"Sub");
	cmdFUN = GetCommandValue( (unsigned char *)"Function");
    cmdLOCAL = GetCommandValue( (unsigned char *)"Local");
    cmdSTATIC = GetCommandValue( (unsigned char *)"Static");
    cmdENDSUB= GetCommandValue( (unsigned char *)"End Sub");
    cmdENDFUNCTION = GetCommandValue( (unsigned char *)"End Function");
    cmdDO=  GetCommandValue( (unsigned char *)"Do");
    cmdFOR=  GetCommandValue( (unsigned char *)"For");
    cmdNEXT= GetCommandValue( (unsigned char *)"Next");
	cmdIRET = GetCommandValue( (unsigned char *)"IReturn");
    cmdCSUB = GetCommandValue( (unsigned char *)"CSub");
//        PInt(CommandTableSize);
//        PIntComma(TokenTableSize);
//        MMPrintString("\r\n");

}



// run a program
// this will continuously execute a program until the end (marked by TWO zero chars)
// the argument p must point to the first line to be executed
void __not_in_flash_func(ExecuteProgram)(unsigned char *p) {
    int i, SaveLocalIndex = 0;
    skipspace(p);                                                   // just in case, skip any whitespace

    while(1) {
        if(*p == 0) p++;                                            // step over the zero byte marking the beginning of a new element
        if(*p == T_NEWLINE) {
            CurrentLinePtr = p;                                     // and pointer to the line for error reporting
            TraceBuff[TraceBuffIndex] = p;                          // used by TRACE LIST
            if(++TraceBuffIndex >= TRACE_BUFF_SIZE) TraceBuffIndex = 0;
            if(TraceOn && p < ProgMemory + Option.PROG_FLASH_SIZE) {
                inpbuf[0] = '[';
                IntToStr(inpbuf + 1, CountLines(p), 10);
                strcat(inpbuf, "]");
                MMPrintString(inpbuf);
                uSec(1000);
            }
            p++;                                                    // and step over the token
        }
        if(*p == T_LINENBR) p += 3;                                 // and step over the number
        skipspace(p);                                               // and skip any trailing white space
        if(p[0] == T_LABEL) {                                       // got a label
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }

        if(*p) {                                                    // if p is pointing to a command
            nextstmt = cmdline = p + 1;
            skipspace(cmdline);
            skipelement(nextstmt);
            if(*p && *p != '\'') {                                  // ignore a comment line
                if(setjmp(ErrNext) == 0) {                          // return to the else leg of this if error and OPTION ERROR SKIP/IGNORE is in effect
                    SaveLocalIndex = LocalIndex;                    // save this if we need to cleanup after an error
                    if(*(char*)p >= C_BASETOKEN && *(char*)p - C_BASETOKEN < CommandTableSize - 1 && (commandtbl[*(char*)p - C_BASETOKEN].type & T_CMD)) {
                        cmdtoken = *(char*)p;
                        targ = T_CMD;
                        commandtbl[*(char*)p - C_BASETOKEN].fptr(); // execute the command
                    } else {
                        if(!isnamestart(*p)) error("Invalid character: @", (int)(*p));
                        i = FindSubFun(p, false);                   // it could be a defined command
                        if(i >= 0) {                                // >= 0 means it is a user defined command
                            DefinedSubFun(false, p, i, NULL, NULL, NULL, NULL);
                        }
                        else
                            error("Unknown command");
                    }
                } else {
                    LocalIndex = SaveLocalIndex;                    // restore so that we can clean up any memory leaks
                    ClearTempMemory();
                }
                if(OptionErrorSkip > 0) OptionErrorSkip--;        // if OPTION ERROR SKIP decrement the count - we do not error if it is greater than zero
                if(TempMemoryIsChanged) ClearTempMemory();          // at the end of each command we need to clear any temporary string vars
                check_interrupt();                                  // check for an MMBasic interrupt or touch event and handle it
                CheckAbort();
            }
            p = nextstmt;
        }
        if((p[0] == 0 && p[1] == 0) || (p[0] == 0xff && p[1] == 0xff)) return;      // the end of the program is marked by TWO zero chars, empty flash by two 0xff
    }
}


/********************************************************************************************************************************************
 Code associated with processing user defined subroutines and functions
********************************************************************************************************************************************/


// Scan through the program loaded in flash and build a table pointing to the definition of all user defined subroutines and functions.
// This pre processing speeds up the program when using defined subroutines and functions
// this routine also looks for embedded fonts and adds them to the font table
void  PrepareProgram(int ErrAbort) {
    int i, j, NbrFuncts;
    unsigned char *p1, *p2;
    for(i = FONT_BUILTIN_NBR; i < FONT_TABLE_SIZE; i++)
        FontTable[i] = NULL;                                        // clear the font table

    
    NbrFuncts = 0;
    CFunctionFlash = CFunctionLibrary = NULL;



    PrepareProgramExt(ProgMemory, NbrFuncts,&CFunctionFlash, ErrAbort);
    
    // check the sub/fun table for duplicates
    if(!ErrAbort) return;
    for(i = 0; i < MAXSUBFUN && subfun[i] != NULL; i++) {
        for(j = i + 1; j < MAXSUBFUN && subfun[j] != NULL; j++) {
            CurrentLinePtr = p1 = subfun[i];
            p1++;
            skipspace(p1);
            p2 = subfun[j];
            p2++;
            skipspace(p2);
            while(1) {
                if(!isnamechar(*p1) && !isnamechar(*p2)) {
                    error("Duplicate name");
                    return;
                }
                if(toupper(*p1) != toupper(*p2)) break;
                p1++; p2++;
            }
        }
    }
}


// This scans one area (main program or the library area) for user defined subroutines and functions.
// It is only used by PrepareProgram() above.
int  PrepareProgramExt(unsigned char *p, int i, unsigned char **CFunPtr, int ErrAbort) {
    unsigned int *cfp;
    while(*p != 0xff) {
        p = GetNextCommand(p, &CurrentLinePtr, NULL);
        if(*p == 0) break;                                          // end of the program or module
        if(*p == cmdSUB || *p == cmdFUN /*|| *p == cmdCFUN*/ || *p == cmdCSUB) {         // found a SUB, FUN, CFUNCTION or CSUB token
            if(i >= MAXSUBFUN) {
                if(ErrAbort) error("Too many subroutines and functions");
                continue;
            }
            subfun[i++] = p++;                                      // save the address and step over the token
            skipspace(p);
            if(!isnamestart(*p)) {
                if(ErrAbort) error("Invalid identifier");
                i--;
                continue;
            }
        }
        while(*p) p++;                                              // look for the zero marking the start of the next element
    }
    while(*p == 0) p++;                                             // the end of the program can have multiple zeros
    p++;                                                            // step over the terminating 0xff
    *CFunPtr = (unsigned char *)(((unsigned int)p + 0b11) & ~0b11); // CFunction flash (if it exists) starts on the next word address after the program in flash
    if(i < MAXSUBFUN) subfun[i] = NULL;
    CurrentLinePtr = NULL;
    // now, step through the CFunction area looking for fonts to add to the font table
    cfp = *(unsigned int **)CFunPtr;
    while(*cfp != 0xffffffff) {
        if(*cfp < FONT_TABLE_SIZE)
            FontTable[*cfp] = (unsigned char *)(cfp + 2);
        cfp++;
        cfp += (*cfp + 4) / sizeof(unsigned int);
    }
    return i;
}



// searches the subfun[] table to locate a defined sub or fun
// returns with the index of the sub/function in the table or -1 if not found
// if type = 0 then look for a sub otherwise a function
int __not_in_flash_func(FindSubFun)(unsigned char *p, int type) {
    unsigned char *p1, *p2;
    int i;

    for(i = 0;  i < MAXSUBFUN && subfun[i] != NULL; i++) {
        p2 = subfun[i];                                             // point to the command token
        if(type == 0) {                                             // if it is a sub and we want a fun or vice versa skip this one
            if(!(*p2 == cmdSUB || *p2 == cmdCSUB)) continue;
        } else {
            if(!(*p2 == cmdFUN /*|| *p2 == cmdCFUN*/)) continue;
        }
        p2++; skipspace(p2);                                        // point to the identifier
        if(toupper(*p) != toupper(*p2)) continue;                   // quick first test
        p1 = p + 1;  p2++;
        while(isnamechar(*p1) && toupper(*p1) == toupper(*p2)) { p1++; p2++; };
        if((*p1 == '$' && *p2 == '$') || (*p1 == '%' && *p2 == '%') || (*p1 == '!' && *p2 == '!') || (!isnamechar(*p1) && !isnamechar(*p2))) return i;          // found it !
    }
    return -1;
}



// This function is responsible for executing a defined subroutine or function.
// As these two are similar they are processed in the one lump of code.
//
// The arguments when called are:
//   isfun    = true if we are executing a function
//   cmd      = pointer to the command name used by the caller (in program memory)
//   index    = index into subfun[i] which points to the definition of the sub or funct
//   fa, i64a, sa and typ are pointers to where the return value is to be stored (used by functions only)
void __not_in_flash_func(DefinedSubFun)(int isfun, unsigned char *cmd, int index, MMFLOAT *fa, long long int  *i64a, unsigned char **sa, int *typ) {
	unsigned char *p, *s, *tp, *ttp, tcmdtoken;
	unsigned char *CallersLinePtr, *SubLinePtr = NULL;
    unsigned char *argbuf1; unsigned char **argv1; int argc1;
    unsigned char *argbuf2; unsigned char **argv2; int argc2;
    unsigned char fun_name[MAXVARLEN + 1];
	int i;
    int ArgType, FunType;
    int *argtype;
    union u_argval {
        MMFLOAT f;                                                    // the value if it is a float
        long long int  i;                                            // the value if it is an integer
        MMFLOAT *fa;                                                  // pointer to the allocated memory if it is an array of floats
        long long int  *ia;                                          // pointer to the allocated memory if it is an array of integers
        unsigned char *s;                                                    // pointer to the allocated memory if it is a string
    } *argval;
    int *argVarIndex;

    CallersLinePtr = CurrentLinePtr;
    SubLinePtr = subfun[index];                                     // used for error reporting
    p =  SubLinePtr + 1;                                            // point to the sub or function definition
    skipspace(p);
    ttp = p;
    
    // copy the sub/fun name from the definition into temp storage and terminate
    // p is left pointing to the end of the name (ie, start of the argument list in the definition)
    CurrentLinePtr = SubLinePtr;                                    // report errors at the definition
    tp = fun_name;
    *tp++ = *p++; while(isnamechar(*p)) *tp++ = *p++;
    if(*p == '$' || *p == '%' || *p == '!') {
        if(!isfun) {
            error("Type specification is invalid: @", (int)(*p));
        }
        *tp++ = *p++;
    }
    *tp = 0;
    
    if(isfun && *p != '(' /*&& (*SubLinePtr != cmdCFUN)*/) error("Function definition");

    // find the end of the caller's identifier, tp is left pointing to the start of the caller's argument list
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    tp = cmd + 1;
    while(isnamechar(*tp)) tp++;
    if(*tp == '$' || *tp == '%' || *tp == '!') {
        if(!isfun) error("Type specification");
        tp++;
    }
    if(toupper(*(p-1)) != toupper(*(tp-1))) error("Inconsistent type suffix");

    // if this is a function we check to find if the function's type has been specified with AS <type> and save it
    CurrentLinePtr = SubLinePtr;                                    // report errors at the definition
    FunType = T_NOTYPE;
    if(isfun) {
        ttp = skipvar(ttp, false);                                  // point to after the function name and bracketed arguments
        skipspace(ttp);
        if(*ttp == tokenAS) {                                       // are we using Microsoft syntax (eg, AS INTEGER)?
            ttp++;                                                  // step over the AS token
            ttp = CheckIfTypeSpecified(ttp, &FunType, true);        // get the type
            if(!(FunType & T_IMPLIED)) error("Variable type");
        }
        FunType |= (V_FIND | V_DIM_VAR | V_LOCAL | V_EMPTY_OK);
    }


    // from now on
    // tp  = the caller's argument list
    // p   = the argument list for the definition
    skipspace(tp); skipspace(p);

     // similar if this is a CSUB
    if(*SubLinePtr == cmdCSUB) {
        CallCFunction(SubLinePtr, tp, p, CallersLinePtr);           // run the CSUB
        TempMemoryIsChanged = true;                                 // signal that temporary memory should be checked
        return;
    }
   // from now on we have a user defined sub or function (not a C routine)
    
    if(gosubindex >= MAXGOSUB) error("Too many nested SUB/FUN");
    errorstack[gosubindex] = CallersLinePtr;
	gosubstack[gosubindex++] = isfun ? NULL : nextstmt;             // NULL signifies that this is returned to by ending ExecuteProgram()

    // allocate memory for processing the arguments
    argval = GetTempMemory(MAX_ARG_COUNT * sizeof(union u_argval));
    argtype = GetTempMemory(MAX_ARG_COUNT * sizeof(int));
    argVarIndex = GetTempMemory(MAX_ARG_COUNT * sizeof(int));
    argbuf1 = GetTempMemory(STRINGSIZE); 
    argv1 = GetTempMemory(MAX_ARG_COUNT * sizeof(unsigned char *));  // these are for the caller
    argbuf2 = GetTempMemory(STRINGSIZE); 
    argv2 = GetTempMemory(MAX_ARG_COUNT * sizeof(unsigned char *));  // and these for the definition of the sub or function

    // now split up the arguments in the caller
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    argc1 = 0;
    if(*tp) makeargs(&tp, MAX_ARG_COUNT, argbuf1, argv1, &argc1, (*tp == '(') ? (unsigned char *)"(," : (unsigned char *)",");

    // split up the arguments in the definition
    CurrentLinePtr = SubLinePtr;                                    // any errors must be at the definition
    argc2 = 0;
    if(*p) makeargs(&p, MAX_ARG_COUNT, argbuf2, argv2, &argc2, (*p == '(') ? (unsigned char *)"(," : (unsigned char *)",");

    // error checking
    if(argc2 && (argc2 & 1) == 0) error("Argument list");
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    if(argc1 > argc2 || (argc1 && (argc1 & 1) == 0)) error("Argument list");

	// step through the arguments supplied by the caller and get the value supplied
    // these can be:
    //    - missing (ie, caller did not supply that parameter)
    //    - a variable, in which case we need to get a pointer to that variable's data and save its index so later we can get its type
    //    - an expression, in which case we evaluate the expression and get its value and type
    for(i = 0; i < argc2; i += 2) {                                 // count through the arguments in the definition of the sub/fun       
        if(i < argc1 && *argv1[i]) {
            // check if the argument is a valid variable
            if(i < argc1 && isnamestart(*argv1[i]) && *skipvar(argv1[i], false) == 0) {
                // yes, it is a variable (or perhaps a user defined function which looks the same)?
                if(!(FindSubFun(argv1[i], 1) >= 0 && strchr((char *)argv1[i], '(') != NULL)) {
                    // yes, this is a valid variable.  set argvalue to point to the variable's data and argtype to its type
                    argval[i].s = findvar(argv1[i], V_FIND | V_EMPTY_OK);        // get a pointer to the variable's data
                    argtype[i] = vartbl[VarIndex].type;                          // and the variable's type
                    argVarIndex[i] = VarIndex;
                    if(argtype[i] & T_CONST) {
                        argtype[i] = 0;                                          // we don't want to point to a constant
                    } else {
                        argtype[i] |= T_PTR;                                     // flag this as a pointer
                    }
                }
            }

            // if argument is present and is not a pointer to a variable then evaluate it as an expression
            if(argtype[i] == 0) {
                long long int  ia;
                evaluate(argv1[i], &argval[i].f, &ia, &s, &argtype[i], false);   // get the value and type of the argument
                if(argtype[i] & T_INT)
                    argval[i].i = ia;
                else if(argtype[i] & T_STR) {
                    argval[i].s = GetTempMemory(STRINGSIZE);
                    Mstrcpy(argval[i].s, s);
                }
            }
        }
    }

    // now we step through the parameters in the definition of the sub/fun
    // for each one we create the local variable and compare its type to that supplied in the callers list
    CurrentLinePtr = SubLinePtr;                                    // any errors must be at the definition
    LocalIndex++;
    for(i = 0; i < argc2; i += 2) {                                 // count through the arguments in the definition of the sub/fun
        ArgType = T_NOTYPE;
        tp = skipvar(argv2[i], false);                              // point to after the variable
        skipspace(tp);
        if(*tp == tokenAS) {                                        // are we using Microsoft syntax (eg, AS INTEGER)?
            *tp++ = 0;                                              // terminate the string and step over the AS token
            tp = CheckIfTypeSpecified(tp, &ArgType, true);          // and get the type
            if(!(ArgType & T_IMPLIED)) error("Variable type");
        }
        ArgType |= (V_FIND | V_DIM_VAR | V_LOCAL | V_EMPTY_OK);
        tp = findvar(argv2[i], ArgType);                            // declare the local variable
        if(vartbl[VarIndex].dims[0] > 0) error("Argument list");    // if it is an array it must be an empty array
        
        CurrentLinePtr = CallersLinePtr;                            // report errors at the caller

        // if the definition called for an array, special processing and checking will be required
        if(vartbl[VarIndex].dims[0] == -1) {
            int j;
            if(vartbl[argVarIndex[i]].dims[0] == 0) error("Expected an array");
            if(TypeMask(vartbl[VarIndex].type) != TypeMask(argtype[i])) error("Incompatible type: $", argv1[i]);
            vartbl[VarIndex].val.s = NULL;
            for(j = 0; j < MAXDIM; j++)                             // copy the dimensions of the supplied variable into our local variable
                vartbl[VarIndex].dims[j] = vartbl[argVarIndex[i]].dims[j];
        }

        // if this is a pointer check and the type is NOT the same as that requested in the sub/fun definition
        if((argtype[i] & T_PTR) && TypeMask(vartbl[VarIndex].type) != TypeMask(argtype[i])) {
            if((TypeMask(vartbl[VarIndex].type) & T_STR) || (TypeMask(argtype[i]) & T_STR))
                error("Incompatible type: $", argv1[i]);
            // make this into an ordinary argument
            if(vartbl[argVarIndex[i]].type & T_PTR) {
                argval[i].i = *vartbl[argVarIndex[i]].val.ia;       // get the value if the supplied argument is a pointer
            } else {
                argval[i].i = *(long long int *)argval[i].s;        // get the value if the supplied argument is an ordinary variable
            }
            argtype[i] &= ~T_PTR;                                   // and remove the pointer flag
        }
        
        // if this is a pointer (note: at this point the caller type and the required type must be the same)
        if(argtype[i] & T_PTR) {
            // the argument supplied was a variable so we must setup the local variable as a pointer
            if((vartbl[VarIndex].type & T_STR) && vartbl[VarIndex].val.s != NULL) {
                FreeMemory(vartbl[VarIndex].val.s);                            // free up the local variable's memory if it is a pointer to a string
                }
            vartbl[VarIndex].val.s = argval[i].s;                              // point to the data of the variable supplied as an argument
            vartbl[VarIndex].type |= T_PTR;                                    // set the type to a pointer
            vartbl[VarIndex].size = vartbl[argVarIndex[i]].size;               // just in case it is a string copy the size
        // this is not a pointer
        } else if(argtype[i] != 0) {                                           // in getting the memory argtype[] is initialised to zero
            // the parameter was an expression or a just straight variables with different types (therefore not a pointer))
            if((vartbl[VarIndex].type & T_STR) && (argtype[i] & T_STR)) {      // both are a string
                Mstrcpy((char *)vartbl[VarIndex].val.s, (char *)argval[i].s);
                ClearSpecificTempMemory(argval[i].s);
            } else if((vartbl[VarIndex].type & T_NBR) && (argtype[i] & T_NBR)) // both are a float
                vartbl[VarIndex].val.f = argval[i].f;
            else if((vartbl[VarIndex].type & T_NBR) && (argtype[i] & T_INT))   // need a float but supplied an integer
                vartbl[VarIndex].val.f = argval[i].i;
            else if((vartbl[VarIndex].type & T_INT) && (argtype[i] & T_INT))   // both are integers
                vartbl[VarIndex].val.i = argval[i].i;
            else if((vartbl[VarIndex].type & T_INT) && (argtype[i] & T_NBR))   // need an integer but was supplied with a float
                vartbl[VarIndex].val.i = FloatToInt64(argval[i].f);
            else
                error("Incompatible type: $", argv1[i]);
        }
    }

    // temp memory used in setting up the arguments can be deleted now
    ClearSpecificTempMemory(argval); ClearSpecificTempMemory(argtype); ClearSpecificTempMemory(argVarIndex);
    ClearSpecificTempMemory(argbuf1); ClearSpecificTempMemory(argv1);
    ClearSpecificTempMemory(argbuf2); ClearSpecificTempMemory(argv2);
   
    strcpy((char *)CurrentSubFunName, (char *)fun_name);
    // if it is a defined command we simply point to the first statement in our command and allow ExecuteProgram() to carry on as before
    // exit from the sub is via cmd_return which will decrement LocalIndex
    if(!isfun) {
        skipelement(p);
        nextstmt = p;                                               // point to the body of the subroutine
        return;
    }

    // if it is a defined function we have a lot more work to do.  We must:
    //   - Create a local variable for the function's name
    //   - Save the globals being used by the current command that caused the function to be called
    //   - Invoke another instance of ExecuteProgram() to execute the body of the function
    //   - When that returns we need to restore the global variables
    //   - Get the variable's value and save that in the return value globals (fret or sret)
    //   - Return to the expression parser
    tp = findvar(fun_name, FunType | V_FUNCT);                      // declare the local variable
    FunType = vartbl[VarIndex].type;
    if(FunType & T_STR) {
        FreeMemory(vartbl[VarIndex].val.s);                         // free the memory if it is a string
        vartbl[VarIndex].type |= T_PTR;
        LocalIndex--;                                               // allocate the memory at the previous level
        vartbl[VarIndex].val.s = tp = GetTempMemory(STRINGSIZE);    // and use our own memory
        LocalIndex++;
    }
    skipelement(p);                                                 // point to the body of the function

    ttp = nextstmt;                                                 // save the globals used by commands
    tcmdtoken = cmdtoken;
    s = cmdline;

    ExecuteProgram(p);                                              // execute the function's code
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller

    cmdline = s;                                                    // restore the globals
    cmdtoken = tcmdtoken;
    nextstmt = ttp;

    // return the value of the function's variable to the caller
    if(FunType & T_NBR)
        *fa = *(MMFLOAT *)tp;
    else if(FunType & T_INT)
        *i64a = *(long long int  *)tp;
    else
        *sa = tp;                                                   // for a string we just need to return the local memory
    *typ = FunType;                                                 // save the function type for the caller
	ClearVars(LocalIndex--);                                        // delete any local variables
    TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
	gosubindex--;
}

char *strcasechr(const char *p, int ch)
{
	char c;

	c = toupper(ch);
	for (;; ++p) {
		if (toupper(*p) == c)
			return ((char *)p);
		if (*p == '\0')
			return (NULL);
	}
	/* NOTREACHED */
}

char *fstrstr (const char *s1, const char *s2)
{
  const char *p = s1;
  const size_t len = strlen (s2);

  for (; (p = strcasechr (p, *s2)) != 0; p++)
    {
      if (strncasecmp (p, s2, len) == 0)
	return (char *)p;
    }
  return (0);
}

void str_replace(char *target, const char *needle, const char *replacement)
{
    char buffer[288] = { 0 };
    char *insert_point = &buffer[0];
    const char *tmp = target;
    size_t needle_len = strlen(needle);
    size_t repl_len = strlen(replacement);

    while (1) {
        const char *p = fstrstr(tmp, needle);

        // walked past last occurrence of needle; copy remaining part
        if (p == NULL) {
            strcpy(insert_point, tmp);
            break;
        }

        // copy part before needle
        memcpy(insert_point, tmp, p - tmp);
        insert_point += p - tmp;

        // copy replacement string
        memcpy(insert_point, replacement, repl_len);
        insert_point += repl_len;

        // adjust pointers, move on
        tmp = p + needle_len;
    }

    // write altered string back to target
    strcpy(target, buffer);
}

void STR_REPLACE(char *target, const char *needle, const char *replacement){
	char *ip=target;
	int toggle=0;
	while(*ip){
		if(*ip==34){
			if(toggle==0)toggle=1;
			else toggle=0;
		}
		if(toggle && *ip==' '){
			*ip=0xFF;
		}
		if(toggle && *ip=='.'){
			*ip=0xFE;
		}
		if(toggle && *ip=='='){
			*ip=0xFD;
		}
		ip++;
	}
	str_replace(target, needle, replacement);
	ip=target;
	while(*ip){
		if(*ip==0xFF)*ip=' ';
		if(*ip==0xFE)*ip='.';
		if(*ip==0xFD)*ip='=';
		ip++;
	}

}


/********************************************************************************************************************************************
 take an input line and turn it into a line with tokens suitable saving into memory
********************************************************************************************************************************************/

//take an input string in inpbuf[] and copy it to tknbuf[] and:
// - convert the line number to a binary number
// - convert a label to the token format
// - convert keywords to tokens
// - convert the colon to a zero char
//the result in tknbuf[] is terminated with MMFLOAT zero chars
// if the arg console is true then do not add a line number

void  tokenise(int console) {
    unsigned char *p, *op, *tp;
    int i;
    int firstnonwhite;
    int labelvalid;

    // first, make sure that only printable characters are in the line
    p = inpbuf;
    while(*p) {
        *p = *p & 0x7f;
        if(*p < ' ' || *p == 0x7f)  *p = ' ';
        p++;
    }
    STR_REPLACE(inpbuf,"MM.INFO$","MM.INFO");
    STR_REPLACE(inpbuf,"=>",">=");
    STR_REPLACE(inpbuf,"=<","<=");
    STR_REPLACE(inpbuf,"MM.FONTHEIGHT","MM.INFO(FONTHEIGHT)");
    STR_REPLACE(inpbuf,"MM.FONTWIDTH","MM.INFO(FONTWIDTH)");

    // setup the input and output buffers
    p = inpbuf;
    op = tknbuf;
    if(!console) *op++ = T_NEWLINE;

    // get the line number if it exists
    tp = p;
    skipspace(tp);
    for(i = 0; i < 8; i++) if(!isxdigit(tp[i])) break;              // test if this is eight hex digits
    if(isdigit(*tp) && i < 8) {                                     // if it a digit and not an 8 digit hex number (ie, it is CFUNCTION data) then try for a line number
        i = strtol(tp, (char **)&tp, 10);
        if(!console && i > 0 && i <= MAXLINENBR) {
            *op++ = T_LINENBR;
            *op++ = (i>>8);
            *op++ = (i & 0xff);
        }
        p = tp;
    }

    // process the rest of the line
    firstnonwhite = true;
    labelvalid = true;
    while(*p) {

        // just copy a space
        if(*p == ' ') {
            *op++ = *p++;
            continue;
        }

        // first look for quoted text and copy it across
        // this will also accept a string without the closing quote and it will add the quote in
        if(*p == '"') {
            do {
                *op++ = *p++;
            } while(*p != '"' && *p);
            *op++ = '"';
            if(*p == '"') p++;
            continue;
        }

        // copy anything after a comment (')
        if(*p == '\'') {
            do {
                *op++ = *p++;
            } while(*p);
            continue;
        }

        // check for multiline separator (colon) and replace with a zero char
        if(*p == ':') {
            *op++ = 0;
            p++;
            while(*p == ':') {                                      // insert a space between consecutive colons
                *op++ = ' ';
                *op++ = 0;
                p++;
            }
            firstnonwhite = true;
            continue;
        }

        // not whitespace or string or comment  - try a number
        if(isdigit(*p) || *p == '.') {                              // valid chars at the start of a number
            while(isdigit(*p) || *p == '.' || *p == 'E' || *p == 'e')
                if (*p == 'E' || *p == 'e') {   // check for '+' or '-' as part of the exponent
                    *op++ = *p++;                                   // copy the number
                    if (*p == '+' || *p == '-') {                   // BUGFIX by Gerard Sexton
                        *op++ = *p++;                               // copy the '+' or '-'
                    }
                } else {
                    *op++ = *p++;                                   // copy the number
                }
            firstnonwhite = false;
            continue;
        }

        // not whitespace or string or comment or number - see if we can find a label or a token identifier
         if(firstnonwhite) {                                         // first entry on the line must be a command
            // these variables are only used in the search for a command code
            unsigned char *tp2, *match_p = NULL;
            int match_i = -1, match_l = 0;
            // first test if it is a print shortcut char (?) - this needs special treatment
            if(*p == '?') {
                match_i = GetCommandValue("Print") - C_BASETOKEN;
                if(*++p == ' ') p++;                                // eat a trailing space
                match_p = p;
             // translate US spelling of COLOUR and therefor save a token slot
            } else if((tp2 = checkstring(p, "COLOR")) != NULL) {
                    match_i = GetCommandValue("Colour") - C_BASETOKEN;
                    match_p = p = tp2;
            } else if((tp2 = checkstring(p, "ELSE IF")) != NULL) {
                    match_i = GetCommandValue("ElseIf") - C_BASETOKEN;
                    match_p = p = tp2;
            } else if((tp2 = checkstring(p, "END IF")) != NULL) {
                    match_i = GetCommandValue("EndIf") - C_BASETOKEN;
                    match_p = p = tp2;
            } else if((tp2 = checkstring(p, "EXIT DO")) != NULL) {
                    match_i = GetCommandValue("Exit") - C_BASETOKEN;
                    match_p = p = tp2;
            } else if((tp2 = checkstring(p, "CAT")) != NULL) {
                    match_i = GetCommandValue("Inc") - C_BASETOKEN;
                    match_p = p = tp2;
            } else {
                // now try for a command in the command table
                // this works by scanning the entire table looking for the match with the longest command name
                // this is needed because we need to differentiate between END and END SUB for example.
                // without looking for the longest match we might think that we have a match when we found just END.
            for(i = 0 ; i < CommandTableSize - 1; i++) {
                    tp2 = p;
                    tp = (char *)commandtbl[i].name;
                    while(toupper(*tp2) == toupper(*tp) && *tp != 0) {
                        if(*tp == ' ')
                            skipspace(tp2);                         // eat up any extra spaces between keywords
                        else
                            tp2++;
                        tp++;
                        if(*tp == '(') skipspace(tp2);              // eat up space between a keyword and bracket
                    }
                    // we have a match
                    if(*tp == 0 && (!isnamechar(*tp2) || (commandtbl[i].type & T_FUN))) {
                        if(*(tp - 1) != '(' && isnamechar(*tp2)) continue;   // skip if not the function
                        // save the details if it is the longest command found so far
                        if(strlen(commandtbl[i].name) > match_l) {
                            match_p = tp2;
                            match_l = strlen(commandtbl[i].name);
                            match_i = i;
                        }
                    }
                }
            }

            if(match_i > -1) {
                // we have found a command
                *op++ = match_i + C_BASETOKEN;                      // insert the token found
                p = match_p;                                        // step over the command in the source
                if(isalpha(*(p-1)) && *p == ' ') p++;               // if the command is followed by a space skip over it
                if(match_i + C_BASETOKEN == GetCommandValue("Rem")) // check if it is a REM command
                    while(*p) *op++ = *p++;                         // and in that case just copy everything
                firstnonwhite = false;
                labelvalid = false;                                 // we do not want any labels after this
                continue;
            }

            // next test if it is a label
            if(labelvalid && isnamestart(*p)) {
                for(i = 0, tp = p + 1; i < MAXVARLEN - 1; i++, tp++)
                    if(!isnamechar(*tp)) break;                     // search for the first invalid char
                if(*tp == ':') {                                    // Yes !!  It is a label
                    labelvalid = false;                             // we do not want any more labels
                    *op++ = T_LABEL;                                // insert the token
                    *op++ = tp - p;                                 // insert the length of the label
                    for(i = tp - p; i > 0; i--) *op++ = *p++;       // copy the label
                    p++;                                            // step over the terminating colon
                    continue;
                }
            }
        } else {
            // check to see if it is a function or keyword
            unsigned char *tp2 = NULL;
            for(i = 0 ; i < TokenTableSize - 1; i++) {
                tp2 = p;
                tp = (char *)tokentbl[i].name;
                // check this entry
                while(toupper(*tp2) == toupper(*tp) && *tp != 0) {
                    tp++; tp2++;
                    if(*tp == '(') skipspace(tp2);
                }
                if(*tp == 0 && (!isnameend(*(tp - 1)) || !isnamechar(*tp2))) break;
            }
            if(i != TokenTableSize - 1) {
                // we have a  match
                i += C_BASETOKEN;
                *op++ = i;                                          // insert the token found
                p = tp2;                                            // and step over it in the source text
                        if(i == tokenTHEN || i == tokenELSE)
                            firstnonwhite = true;                   // a command is valid after a THEN or ELSE
                        else
                            firstnonwhite = false;
                continue;
            }
        }

        // not whitespace or string or comment or token identifier or number
        // try for a variable name which could be a user defined subroutine or an implied let
        if(isnamestart(*p)) {                                       // valid chars at the start of a variable name
            if(firstnonwhite) {                                     // first entry on the line?
                tp = skipvar(p, true);                              // find the char after the variable
                skipspace(tp);
                if(*tp == '=')                                      // is it an implied let?
                    *op++ = GetCommandValue((unsigned char *)"Let");   // find let's token value and copy into memory
                }
            while(isnamechar(*p)) *op++ = *p++;                     // copy the variable name
            firstnonwhite = false;
            labelvalid = false;                                     // we do not want any labels after this
            continue;
        }

        // special case where the character to copy is an opening parenthesis
        // we search back to see if the previous non space char was the end of an identifier and, if it is, we remove any spaces following the identifier
        // this enables the programmer to put spaces after a function name or array identifier without causing a confusing error
        if(*p == '(') {
            tp = op - 1;
            if(*tp == ' ') {
                while(*tp == ' ') tp--;
                if(isnameend(*tp)) op = tp + 1;
            }
        }

        // something else, so just copy the one character
        *op++ = *p++;
       labelvalid = false;                                          // we do not want any labels after this
       firstnonwhite = false;

    }
    // end of loop, trim any trailing blanks (but not part of a line number)
    while(*(op - 1) == ' ' && op > tknbuf + 3) *--op = 0;
    // make sure that it is terminated properly
    *op++ = 0;  *op++ = 0;  *op++ = 0;                              // terminate with  zero chars
}




/********************************************************************************************************************************************
 routines for evaluating expressions
 the main functions are getnumber(), getinteger() and getstring()
********************************************************************************************************************************************/



// A convenient way of evaluating an expression
// it takes two arguments:
//     p = pointer to the expression in memory (leading spaces will be skipped)
//     t = pointer to the type
//         if *t = T_STR or T_NBR or T_INT will throw an error if the result is not the correct type
//         if *t = T_NOTYPE it will not throw an error and will return the type found in *t
// it returns with a void pointer to a float, integer or string depending on the value returned in *t
// this will check that the expression is terminated correctly and throw an error if not
void __not_in_flash_func(*DoExpression)(unsigned char *p, int *t) {
    static MMFLOAT f;
    static long long int  i64;
    static unsigned char *s;

    evaluate(p, &f, &i64, &s, t, false);
    if(*t & T_INT) return &i64;
    if(*t & T_NBR) return &f;
    if(*t & T_STR) return s;

    error("Internal fault (sorry)");
    return NULL;                                                    // to keep the compiler happy
}



// evaluate an expression.  p points to the start of the expression in memory
// returns either the float or string in the pointer arguments
// *t points to an integer which holds the type of variable we are looking for
//  if *t = T_STR or T_NBR or T_INT will throw an error if the result is not the correct type
//  if *t = T_NOTYPE it will not throw an error and will return the type found in *t
// this will check that the expression is terminated correctly and throw an error if not.  flags & E_NOERROR will suppress that check
unsigned char __not_in_flash_func(*evaluate)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *ta, int flags) {
    int o;
    int t = *ta;
    unsigned char *s;

    p = getvalue(p, fa, ia, &s, &o, &t);                            // get the left hand side of the expression, the operator is returned in o
    while(o != E_END) p = doexpr(p, fa, ia, &s, &o, &t);            // get the right hand side of the expression and evaluate the operator in o

    // check that the types match and convert them if we can
    if((*ta & (T_NBR |T_INT)) && t & T_STR) error("Expected a number");
    if(*ta & T_STR && (t & (T_NBR | T_INT))) error("Expected a string");
    if(o != E_END) error("Argument count");
    if((*ta & T_NBR) && (t & T_INT)) *fa = *ia;
    if((*ta & T_INT) && (t & T_NBR)) *ia = FloatToInt64(*fa);
    *ta = t;
    *sa = s;

    // check that the expression is terminated correctly
    if(!(flags & E_NOERROR)) {
        skipspace(p);
        if(!(*p == 0 || *p == ',' || *p == ')' || *p == '\''))  error("Expression syntax");
    }
    return p;
}


// evaluate an expression to get a number
MMFLOAT __not_in_flash_func(getnumber)(unsigned char *p) {
    int t = T_NBR;
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;
    evaluate(p, &f, &i64, &s, &t, false);
    if(t & T_INT) return (MMFLOAT)i64;
    return f;
}


// evaluate an expression and return a 64 bit integer
long long int  __not_in_flash_func(getinteger)(unsigned char *p) {
    int t = T_INT;
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;

    evaluate(p, &f, &i64, &s, &t, false);
    if(t & T_NBR) return FloatToInt64(f);
    return i64;
}



// evaluate an expression and return an integer
// this will throw an error is the integer is outside a specified range
// this will correctly round the number if it is a fraction of an integer
long long int __not_in_flash_func(getint)(unsigned char *p, long long int min, long long int max) {
    long long int  i;
    i = getinteger(p);
    if(i < min || i > max) error("% is invalid (valid is % to %)", (int)i, (int)min, (int)max);
    return i;
}



// evaluate an expression to get a string
unsigned char __not_in_flash_func(*getstring)(unsigned char *p) {
    int t = T_STR;
    MMFLOAT f;
    long long int  i64;
    unsigned char *s;

    evaluate(p, &f, &i64, &s, &t, false);
    return s;
}



// evaluate an expression to get a string using the C style for a string
// as against the MMBasic style returned by getstring()
unsigned char __not_in_flash_func(*getCstring)(unsigned char *p) {
    unsigned char *tp;
    tp = GetTempMemory(STRINGSIZE);                                        // this will last for the life of the command
    Mstrcpy(tp, getstring(p));                                      // get the string and save in a temp place
    MtoC(tp);                                                       // convert to a C style string
    return tp;
}



// recursively evaluate an expression observing the rules of operator precedence
unsigned char __not_in_flash_func(*doexpr)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *oo, int *ta) {
    MMFLOAT fa1, fa2;
    long long int  ia1, ia2;
    int o1, o2;
    int t1, t2;
    unsigned char *sa1, *sa2;

    TestStackOverflow();                                            // throw an error if we have overflowed the PIC32's stack

    fa1 = *fa;
    ia1 = *ia;
    sa1 = *sa;
    t1 = TypeMask(*ta);
    o1 = *oo;
    p = getvalue(p, &fa2, &ia2, &sa2, &o2, &t2);
    while(1) {
        if(o2 == E_END || tokentbl[o1].precedence <= tokentbl[o2].precedence) {
            if((t1 & T_STR) != (t2 & T_STR)) error("Incompatible types in expression");
            targ = tokentbl[o1].type & (T_NBR | T_INT);
            if(targ == T_NBR) {                                     // if the operator does not work with ints convert the args to floats
                if(t1 & T_INT) { fa1 = ia1; t1 = T_NBR; }           // at this time the only example of this is op_div (/)
                if(t2 & T_INT) { fa2 = ia2; t2 = T_NBR; }
            }
            if(targ == T_INT) {                                     // if the operator does not work with floats convert the args to ints
                if(t1 & T_NBR) { ia1 = FloatToInt64(fa1); t1 = T_INT; }
                if(t2 & T_NBR) { ia2 = FloatToInt64(fa2); t2 = T_INT; }
            }
            if(targ == (T_NBR | T_INT)) {                               // if the operator will work with both floats and ints
                if(t1 & T_NBR && t2 & T_INT) { fa2 = ia2; t2 = T_NBR; } // if one arg is float convert the other to a float
                if(t1 & T_INT && t2 & T_NBR) { fa1 = ia1; t1 = T_NBR; }
            }
            if(!(tokentbl[o1].type & T_OPER) || !(tokentbl[o1].type & t1)) {
                error("Invalid operator");
            }
            farg1 = fa1; farg2 = fa2;                               // setup the float args (incase it is a float)
            sarg1 = sa1; sarg2 = sa2;                               // ditto string args
            iarg1 = ia1; iarg2 = ia2;                               // ditto integer args
            targ = t1;                                              // this is what both args are
            tokentbl[o1].fptr();                                    // call the operator function
            *fa = fret;
            *ia = iret;
            *sa = sret;
            *oo = o2;
            *ta = targ;
            return p;
        }
        // the next operator has a higher precedence, recursive call to evaluate it
        else
            p = doexpr(p, &fa2, &ia2, &sa2, &o2, &t2);
    }
}



// get a value, either from a constant, function or variable
// also returns the next operator to the right of the value or E_END if no operator
unsigned char __not_in_flash_func(*getvalue)(unsigned char *p, MMFLOAT *fa, long long int  *ia, unsigned char **sa, int *oo, int *ta) {
    MMFLOAT f = 0;
    long long int  i64 = 0;
    unsigned char *s = NULL;
    int t = T_NOTYPE;
    unsigned char *tp, *p1, *p2;
    int i;

    TestStackOverflow();                                            // throw an error if we have overflowed the PIC32's stack

    skipspace(p);
    if(*p>=C_BASETOKEN){ //don't waste time if not a built-in function
    // special processing for the NOT operator
    // just get the next value and invert its logical value
    if(tokenfunction(*p) == op_not) {
        int ro;
        p++; t = T_NOTYPE;
        p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
        if(t &T_NBR)
            f = (MMFLOAT)((f != 0)?0:1);                              // invert the value returned
        else if(t & T_INT)
            i64 = ((i64 != 0)?0:1);
       else
            error("Expected a number");
        skipspace(p);
        *fa = f;                                                    // save what we have
        *ia = i64;
        *sa = s;
        *ta = t;
        *oo = ro;
        return p;                                                   // return straight away as we already have the next operator
    }
		if(tokenfunction(*p) == op_inv) {
			int ro;
			p++; t = T_NOTYPE;
			p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
			if(t & T_NBR)
				i64 = FloatToInt64(f);
			else if(!(t & T_INT))
				error("Expected a number");
			i64 = ~i64;
			t = T_INT;
			skipspace(p);
			*fa = f;                                                    // save what we have
			*ia = i64;
			*sa = s;
			*ta = t;
			*oo = ro;
			return p;                                                   // return straight away as we already have the next operator
		}

		// special processing for the unary - operator
    // just get the next value and negate it
    if(tokenfunction(*p) == op_subtract) {
        int ro;
        p++; t = T_NOTYPE;
        p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
        if(t & T_NBR)
            f = -f;                                                 // negate the MMFLOAT returned
        else if(t & T_INT)
            i64 = -i64;                                             // negate the integer returned
       else
            error("Expected a number");
        skipspace(p);
        *fa = f;                                                    // save what we have
        *ia = i64;
        *sa = s;
        *ta = t;
        *oo = ro;
        return p;                                                   // return straight away as we already have the next operator
    }
		if(tokenfunction(*p) == op_add) {
			int ro;
			p++; t = T_NOTYPE;
			p = getvalue(p, &f, &i64, &s, &ro, &t);                     // get the next value
			skipspace(p);
			*fa = f;                                                    // save what we have
			*ia = i64;
			*sa = s;
			*ta = t;
			*oo = ro;
			return p;                                                   // return straight away as we already have the next operator
		}


    // if a function execute it and save the result
    if(tokentype(*p) & (T_FUN | T_FNA)) {
        int tmp;
        tp = p;
        // if it is a function with arguments we need to locate the closing bracket and copy the argument to
        // a temporary variable so that functions like getarg() will work.
        if(tokentype(*p) & T_FUN) {
            p1 = p + 1;
            p = getclosebracket(p);                             // find the closing bracket
            p2 = ep = GetTempMemory(STRINGSIZE);                           // this will last for the life of the command
            while(p1 != p) *p2++ = *p1++;
        }
        p++;                                                        // point to after the function (without argument) or after the closing bracket
        tmp = targ = TypeMask(tokentype(*tp));                      // set the type of the function (which might need to know this)
        tokenfunction(*tp)();                                       // execute the function
        if((tmp & targ) == 0) error("Internal fault (sorry)");      // as a safety check the function must return a type the same as set in the header
        t = targ;                                                   // save the type of the function
        f = fret; i64 = iret; s = sret;                             // save the result
    }
    } else {
    // if it is a variable or a defined function, find it and get its value
    	if(isnamestart(*p)) {
        // first check if it is terminated with a bracket
        tp = p + 1;
        while(isnamechar(*tp)) tp++;                                // search for the end of the identifier
        if(*tp == '$' || *tp == '%' || *tp == '!') tp++;
        i = -1;
        if(*tp == '(') i = FindSubFun(p, 1);                        // if terminated with a bracket it could be a function
        if(i >= 0) {                                                // >= 0 means it is a user defined function
            unsigned char *SaveCurrentLinePtr = CurrentLinePtr;              // in case the code in DefinedSubFun messes with this
            DefinedSubFun(true, p, i, &f, &i64, &s, &t);
            CurrentLinePtr = SaveCurrentLinePtr;
        } else {
            s = (unsigned char *)findvar(p, V_FIND);                         // if it is a string then the string pointer is automatically set
            t = TypeMask(vartbl[VarIndex].type);
            if(t & T_NBR) f = (*(MMFLOAT *)s);
            if(t & T_INT) i64 = (*(long long int  *)s);
        }
        p = skipvar(p, false);
    }
   // is it an ordinary numeric constant?  get its value if yes
    // a leading + or - might have been converted to a token so we need to check for them also
    else if(isdigit(*p) || *p == '.') {
			char ts[31], *tsp;
			int isi64 = true;
			tsp = ts;
			int isf=true;
			long long int scale=0;
			// copy the first digit of the string to a temporary place
			if(*p == '.') {
				isi64 = false;
				scale=1;
			} else if(isdigit(*p)){
				i64=(*p - '0');
			}
			*tsp++ = *p++;

			// now concatenate the remaining digits
	        while(((*p >= '0' && *p <= '9') || toupper(*p) == 'E' || *p == '-' || *p == '+' || *p == '.') && (tsp - ts) < 30) {
				if(*p >= '0' && *p <= '9'){
					i64 = i64 * 10 + (*p - '0');
					if(scale)scale*=10;
				} else {
					if((*p) == '.'){
						isi64 = false;
						scale =1;
					} else {
						if(toupper(*p) == 'E' || *p == '-' || *p == '+' ){
							isi64 = false;
							isf=false;
						}
					}
				}
				*tsp++ = *p++;                                          // copy the string to a temporary place
			}
			*tsp = 0;                                                   // terminate it
			if(isi64) {
				t = T_INT;
			} else if(isf && (tsp - ts) < 18) {
				f=(MMFLOAT)i64/(MMFLOAT)scale;
				t = T_NBR;
			} else {
				f = (MMFLOAT)strtod(ts, &tsp);                          // and convert to a MMFLOAT
				t = T_NBR;
			}
    }


   // if it is a numeric constant starting with the & character then get its base and convert to an integer
    else if(*p == '&') {
        p++; i64 = 0;
        switch(toupper(*p++)) {
            case 'H':   while(isxdigit(*p)) {
                            i64 = (i64 << 4) | ((toupper(*p) >= 'A') ? toupper(*p) - 'A' + 10 : *p - '0');
                            p++;
                        } break;
            case 'O':   while(*p >= '0' && *p <= '7') {
                            i64 = (i64 << 3) | (*p++ - '0');
                        } break;
            case 'B':   while(*p == '0' || *p == '1') {
                            i64 = (i64 << 1) | (*p++ - '0');
                        } break;
            default:    error("Type prefix");
        }
        t = T_INT;
    }
		// if opening bracket then first evaluate the contents of the bracket
    	else if(*p == '(') {
			p++;                                                        // step over the bracket
			p = evaluate(p, &f, &i64, &s, &t, true);                    // recursively get the contents
			if(*p != ')') error("No closing bracket");
			++p;                                                        // step over the closing bracket
		}
		// if it is a string constant, return a pointer to that.  Note: tokenise() guarantees that strings end with a quote
		else if(*p == '"') {
			p++;                                                        // step over the quote
			p1 = s = GetTempMemory(STRINGSIZE);                                // this will last for the life of the command
			tp = strchr(p, '"');
			while(p != tp) *p1++ = *p++;
			p++;
			CtoM(s);                                                    // convert to a MMBasic string
			t = T_STR;
    }
    else
        error("Syntax");
    }
    skipspace(p);
    *fa = f;                                                        // save what we have
    *ia = i64;
    *sa = s;
    *ta = t;

    // get the next operator, if there is not an operator set the operator to end of expression (E_END)
    if(tokentype(*p) & T_OPER)
        *oo = *p++ - C_BASETOKEN;
    else
        *oo = E_END;

    return p;
}



// search through program memory looking for a line number. Stops when it has a matching or larger number
// returns a pointer to the T_NEWLINE token or a pointer to the two zero characters representing the end of the program
unsigned char *findline(int nbr, int mustfind) {
    unsigned char *p;
    int i;

   p = ProgMemory;
   while(1) {
        if(p[0] == 0 && p[1] == 0) {
            i = MAXLINENBR;
            break;
        }

        if(p[0] == T_NEWLINE) {
            p++;
            continue;
        }

        if(p[0] == T_LINENBR) {
            i = (p[1] << 8) | p[2];
            if(mustfind) {
                if(i == nbr) break;
            } else {
                if(i >= nbr) break;
            }
            p += 3;
            continue;
        }

        if(p[0] == T_LABEL) {
            p += p[1] + 2;
            continue;
        }

        p++;
    }
    if(mustfind && i != nbr)
        error("Line number");
    return p;
}


// search through program memory looking for a label.
// returns a pointer to the T_NEWLINE token or throws an error if not found
// non cached version
unsigned char __not_in_flash_func(*findlabel)(unsigned char *labelptr) {
    unsigned char *p, *lastp = ProgMemory + 1;
    int i;
    unsigned char label[MAXVARLEN + 1];

    // first, just exit we have a NULL argument
    if(labelptr == NULL) return NULL;

    // convert the label to the token format and load into label[]
    // this assumes that the first character has already been verified as a valid label character
    label[1] = *labelptr++;
    for(i = 2; ; i++) {
        if(!isnamechar(*labelptr)) break;                           // the end of the label
        if(i > MAXVARLEN ) error("Label too long");                 // too long, not a correctly formed label
        label[i] = *labelptr++;
    }
    label[0] = i - 1;                                               // the length byte

    // point to the main program memory or the library
    p = ProgMemory;
    
    // now do the search
    while(1) {
        if(p[0] == 0 && p[1] == 0)                                  // end of the program
            error("Cannot find label");

        if(p[0] == T_NEWLINE) {
            lastp = p;                                              // save in case this is the right line
            p++;                                                    // and step over the line number
            continue;
        }

        if(p[0] == T_LINENBR) {
            p += 3;                                                 // and step over the line number
            continue;
        }

        if(p[0] == T_LABEL) {
            p++;                                                    // point to the length of the label
            if(mem_equal(p, label, label[0] + 1))                   // compare the strings including the length byte
                return lastp;                                       // and if successful return pointing to the beginning of the line
            p += p[0] + 1;                                          // still looking! skip over the label
            continue;
        }

        p++;
    }
}



// returns true if 'line' is a valid line in the program
int IsValidLine(int nbr) {
    unsigned char *p;
    p = findline(nbr, false);
    if(*p == T_NEWLINE) p++;
    if(*p == T_LINENBR) {
        if(((p[1] << 8) | p[2]) == nbr) return true;
    }
    return false;
}


// count the number of lines up to and including the line pointed to by the argument
// used for error reporting in programs that do not use line numbers
int CountLines(unsigned char *target) {
    unsigned char *p;
    int cnt;

    p = ProgMemory;
    cnt = 0;

    while(1) {
        if(*p == 0xff || (p[0] == 0 && p[1] == 0))                   // end of the program
            return cnt;

        if(*p == T_NEWLINE) {
            p++;                                                 // and step over the line number
            cnt++;
            if(p >= target) return cnt;
            continue;
        }

        if(*p == T_LINENBR) {
            p += 3;                                                 // and step over the line number
            continue;
        }

        if(*p == T_LABEL) {
            p += p[0] + 2;                                          // still looking! skip over the label
            continue;
        }

        if(p++ > target) return cnt;

    }
}



/********************************************************************************************************************************************
routines for storing and manipulating variables
********************************************************************************************************************************************/


// find or create a variable
// the action parameter can be the following (these can be ORed together)
// - V_FIND    a straight forward find, if the variable is not found it is created and set to zero
// - V_NOFIND_ERR    throw an error if not found
// - V_NOFIND_NULL   return a null pointer if not found
// - V_DIM_VAR    dimension an array
// - V_LOCAL   create a local variable
//
// there are four types of variable:
//  - T_NOTYPE a free slot that was used but is now free for reuse
//  - T_STR string variable
//  - T_NBR holds a float
//  - T_INT integer variable
//
// A variable can have a number of characteristics
//  - T_PTR the variable points to another variable's data
//  - T_IMPLIED  the variables type does not have to be specified with a suffix
//  - T_CONST the contents of this variable cannot be changed
//  - T_FUNCT this variable represents the return value from a function
//
// storage of the variable's data:
//      if it is type T_NBR or T_INT the value is held in the variable slot
//      for T_STR a block of memory of MAXSTRLEN size (or size determined by the LENGTH keyword) will be malloc'ed and the pointer stored in the variable slot.
void __not_in_flash_func(*findvar)(unsigned char *p, int action) {
    unsigned char name[MAXVARLEN + 1];
    int i=0, j, size, ifree, globalifree, localifree, nbr, vtype, vindex, namelen, tmp;
    unsigned char *s, *x, u;
    void *mptr;
//    int hashIndex=0;
    int GlobalhashIndex;
    int LocalhashIndex;
    uint32_t hash=FNV_offset_basis;
	char  *tp, *ip;
    int dim[MAXDIM]={0}, dnbr;
//	if(__get_MSP() < (uint32_t)&stackcheck-0x5000){
//		error("Expression is too complex at depth %",LocalIndex);
//	}
    vtype = dnbr = emptyarray = 0;
    // first zero the array used for holding the dimension values
//    for(i = 0; i < MAXDIM; i++) dim[i] = 0;
    ifree = -1;

    // check the first char for a legal variable name
    skipspace(p);
    if(!isnamestart(*p)) error("Variable name");

    // copy the variable name into name
    s = name; namelen = 0;
	do {
		u=toupper(*p++);
		hash ^= u;
		hash*=FNV_prime;
		*s++ = u;
		if(++namelen > MAXVARLEN) error("Variable name too long");
	} while(isnamechar(*p));
	hash &= MAXHASH; //scale 0-511
	if(namelen!=MAXVARLEN)*s=0;
    // check the terminating char and set the type
    if(*p == '$') {
        if((action & T_IMPLIED) && !(action & T_STR)) error("Conflicting variable type");
        vtype = T_STR;
        p++;
    } else if(*p == '%') {
        if((action & T_IMPLIED) && !(action & T_INT)) error("Conflicting variable type");
        vtype = T_INT;
        p++;
    } else if(*p == '!') {
        if((action & T_IMPLIED) && !(action & T_NBR)) error("Conflicting variable type");
        vtype = T_NBR;
        p++;
    } else if((action & V_DIM_VAR) && DefaultType == T_NOTYPE && !(action & T_IMPLIED))
        error("Variable type not specified");
    else
        vtype = 0;

    // check if this is an array
    if(*p == '(') {
        char *pp = p + 1;
        skipspace(pp);
        if(action & V_EMPTY_OK && *pp == ')')  {                     // if this is an empty array.  eg  ()
        	emptyarray=1;
            dnbr = -1;                                              // flag this
        }  else {                                                      // else, get the dimensions
            // start a new block - getargs macro must be the first executable stmt in a block
            // split the argument into individual elements
            // find the value of each dimension and store in dims[]
            // the bracket in "(," is a signal to getargs that the list is in brackets
            getargs(&p, MAXDIM * 2, "(,");
            if((argc & 0x01) == 0) error("Dimensions");
            dnbr = argc/2 + 1;
            if(dnbr > MAXDIM) error("Dimensions");
            for(i = 0; i < argc; i += 2) {
                MMFLOAT f;
                long long int in;
                char *s;
                int targ = T_NOTYPE;
                evaluate(argv[i], &f, &in, (unsigned char **)&s, &targ, false);       // get the value and type of the argument
                if(targ == T_STR) dnbr = MAXDIM;                    // force an error to be thrown later (with the correct message)
                if(targ == T_NBR) in = FloatToInt32(f);
                dim[i/2] = in;
                if(dim[i/2] < OptionBase) error("Dimensions");
            }
        }
    }

    // we now have the variable name and, if it is an array, the parameters
    // search the table looking for a match

    LocalhashIndex=hash;
    localifree=-1;
    GlobalhashIndex=hash+MAXVARS/2;
	globalifree=-1;
	tmp=-1;
    if(LocalIndex){ //search
		if(vartbl[LocalhashIndex].type == T_NOTYPE){
			localifree = LocalhashIndex;
		} else {
			while(vartbl[LocalhashIndex].name[0]!=0){
				ip=name;
				tp=vartbl[LocalhashIndex].name;
				if(vartbl[LocalhashIndex].type==T_BLOCKED)tmp=LocalhashIndex;
				if(*ip++ == *tp++) {                 // preliminary quick check
					j = namelen-1;
					while(j > 0 && *ip == *tp) {                              // compare each letter
						j--; ip++; tp++;
					}
					if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
						if(vartbl[LocalhashIndex].level == LocalIndex) break; //matching global while not in a subroutine
					}
				}
				LocalhashIndex++;
				LocalhashIndex %= MAXVARS/2;
			}
			if(vartbl[LocalhashIndex].name[0]==0){ // not found
				localifree=LocalhashIndex;
				if(tmp!=-1){
					localifree=tmp;
					vartbl[LocalhashIndex].type=T_NOTYPE;
					vartbl[LocalhashIndex].name[0]=0;
				}
			}
		}
		if(vartbl[LocalhashIndex].name[0]==0){ // not found in the local table so try the global
			tmp=-1;
			globalifree=-1;
			if(vartbl[GlobalhashIndex].type == T_NOTYPE){
				globalifree = GlobalhashIndex;
			} else {
				while(vartbl[GlobalhashIndex].name[0]!=0){
					ip=name;
					tp=vartbl[GlobalhashIndex].name;
					if(vartbl[GlobalhashIndex].type==T_BLOCKED)tmp=GlobalhashIndex;
					if(*ip++ == *tp++) {                 // preliminary quick check
						j = namelen-1;
						while(j > 0 && *ip == *tp) {                              // compare each letter
							j--; ip++; tp++;
						}
						if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
							break; //matching global while not in a subroutine
						}
					}
					GlobalhashIndex++;
					if(GlobalhashIndex==MAXVARS)GlobalhashIndex=MAXVARS/2;
				}
				if(vartbl[GlobalhashIndex].name[0]==0){ // not found
					globalifree=GlobalhashIndex;
					if(tmp!=-1){
						globalifree=tmp;
						vartbl[GlobalhashIndex].type=T_NOTYPE;
						vartbl[GlobalhashIndex].name[0]=0;
					}
				}
			}
		}
    } else {
    	localifree=9999; //set a marker that a local variable is irrelevant
		if(vartbl[GlobalhashIndex].type == T_NOTYPE){
			globalifree = GlobalhashIndex;
		} else {
			while(vartbl[GlobalhashIndex].name[0]!=0){
				ip=name;
				tp=vartbl[GlobalhashIndex].name;
				if(vartbl[GlobalhashIndex].type==T_BLOCKED)tmp=GlobalhashIndex;
				if(*ip++ == *tp++) {                 // preliminary quick check
					j = namelen-1;
					while(j > 0 && *ip == *tp) {                              // compare each letter
						j--; ip++; tp++;
					}
					if(j == 0  && (*(char *)tp == 0 || namelen == MAXVARLEN)) {       // found a matching name
						break; //matching global while not in a subroutine
					}
				}
				GlobalhashIndex++;
				if(GlobalhashIndex==MAXVARS)GlobalhashIndex=MAXVARS/2;
			}
			if(vartbl[GlobalhashIndex].name[0]==0){ // not found
				globalifree=GlobalhashIndex;
				if(tmp!=-1){
					globalifree=tmp;
					vartbl[GlobalhashIndex].type=T_NOTYPE;
					vartbl[GlobalhashIndex].name[0]=0;
				}
			}
		}
    }
//	MMPrintString("search status : ");PInt(LocalIndex);PIntComma(localifree);PIntComma(LocalhashIndex);PIntComma(globalifree);PIntComma(GlobalhashIndex);
//	MMPrintString((action & V_LOCAL ? " LOCAL" : "      "));MMPrintString((action & V_LOCAL ? " DIM" : "    "));PRet();
	// At this point we know if a local variable has been found or if a global variable has been found
    if(action & V_LOCAL) {
        // if we declared the variable as LOCAL within a sub/fun and an existing local was found
        if(localifree==-1) error("$ Local variable already declared", name);
    } else if(action & V_DIM_VAR) {
        // if are using DIM to declare a global variable and an existing global variable was found
        if(globalifree==-1 ) error("$ Global variable already declared", name);
    }
	// we are not declaring the variable but it may need to be created
    if(action & V_LOCAL) {
    	ifree = i = localifree;
    } else if(localifree==-1){ // can only happen when a local variable has been found so we can ignore everything global
		ifree= -1;
		i = LocalhashIndex;
	} else if(globalifree==-1){ //A global variable has been found
		ifree= -1;
		i = GlobalhashIndex;
	} else { //nothing has been found so we are going to create a global unless EXPLICIT is set
		ifree = i = globalifree;
	}

//    MMPrintString(name);PIntComma(i);MMPrintString((ifree==-1 ? " - found" : " - not there"));PRet();

    // if we found an existing and matching variable
    // set the global VarIndex indicating the index in the table
    if(ifree==-1 && vartbl[i].name[0] != 0) {
        VarIndex = vindex = i;

        // check that the dimensions match
        for(i = 0; i < MAXDIM && vartbl[vindex].dims[i] != 0; i++);
        if(dnbr == -1) {
            if(i == 0) error("Array dimensions");
        } else {
            if(i != dnbr) error("Array dimensions");
        }

        if(vtype == 0) {
            if(!(vartbl[vindex].type & (DefaultType | T_IMPLIED))) error("$ Different type already declared", name);
        } else {
            if(!(vartbl[vindex].type & vtype)) error("$ Different type already declared", name);
        }

        // if it is a non arrayed variable or an empty array it is easy, just calculate and return a pointer to the value
        if(dnbr == -1 || vartbl[vindex].dims[0] == 0) {
            if(dnbr == -1 || vartbl[vindex].type & (T_PTR | T_STR))
                return vartbl[vindex].val.s;                        // if it is a string or pointer just return the pointer to the data
            else
                if(vartbl[vindex].type & (T_INT))
                    return &(vartbl[vindex].val.i);                 // must be an integer, point to its value
                else
                    return &(vartbl[vindex].val.f);                 // must be a straight number (float), point to its value
         }

        // if we reached this point it must be a reference to an existing array
        // check that we are not using DIM and that all parameters are within the dimensions
        if(action & V_DIM_VAR) error("Cannot re dimension array");
        for(i = 0; i < dnbr; i++) {
            if(dim[i] > vartbl[vindex].dims[i] || dim[i] < OptionBase)
                error("Index out of bounds");
        }

        // then calculate the index into the array.  Bug fix by Gerard Sexton.
        nbr = dim[0] - OptionBase;
        j = 1;
        for(i = 1; i < dnbr; i++) {
            j *= (vartbl[vindex].dims[i - 1] + 1 - OptionBase);
            nbr += (dim[i] - OptionBase) * j;
        }
        // finally return a pointer to the value
        if(vartbl[vindex].type & T_NBR)
            return vartbl[vindex].val.s + (nbr * sizeof(MMFLOAT));
        else
            if(vartbl[vindex].type & T_INT)
                return vartbl[vindex].val.s + (nbr * sizeof(long long int));
            else
                return vartbl[vindex].val.s + (nbr * (vartbl[vindex].size + 1));
    }

    // we reached this point if no existing variable has been found
    if(action & V_NOFIND_ERR) error("Cannot find $", name);
    if(action & V_NOFIND_NULL) return NULL;
    if((OptionExplicit || dnbr != 0) && !(action & V_DIM_VAR))
        error("$ is not declared", name);
    if(vtype == 0) {
        if(action & T_IMPLIED)
            vtype = (action & (T_NBR | T_INT | T_STR));
        else
            vtype = DefaultType;
    }

    // now scan the sub/fun table to make sure that there is not a sub/fun with the same name
    // now scan the sub/fun table to make sure that there is not a sub/fun with the same name
    if(!(action & V_FUNCT)) {                                       // don't do this if we are defining the local variable for a function name
        for(i = 0;  i < MAXSUBFUN && subfun[i] != NULL; i++) {
            x = subfun[i];                                          // point to the command token
            x++; skipspace(x);                                      // point to the identifier
            s = name;                                               // point to the new variable
            if(*s != toupper(*x)) continue;                         // quick first test
            while(1) {
                if(!isnamechar(*s) && !isnamechar(*x)) error("A sub/fun has the same name: $", name);
                if(*s != toupper(*x) || *s == 0 || !isnamechar(*x) || s - name >= MAXVARLEN) break;
                s++; x++;
            }
        }
    }


    // set a default string size
    size = MAXSTRLEN;

    // if it is an array we must be dimensioning it
    // if it is a string array we skip over the dimension values and look for the LENGTH keyword
    // and if found find the string size and change the vartbl entry
      if(action & V_DIM_VAR) {
          if(vtype & T_STR) {
            i = 0;
            if(*p == '(') {
                do {
                    if(*p == '(') i++;
                    if(tokentype(*p) & T_FUN) i++;
                    if(*p == ')') i--;
                    p++;
                } while(i);
            }
            skipspace(p);
            if((s = checkstring(p, "LENGTH")) != NULL)
                size = getint(s, 1, MAXSTRLEN) ;
            else
                if(!(*p == ',' || *p == 0 || tokenfunction(*p) == op_equal || tokenfunction(*p) == op_invalid)) error("Unexpected text: $", p);
        }
    }


    // at this point we need to create the variable
    // as a result of the previous search ifree is the index to the entry that we should use

 // if we are adding to the top, increment the number of vars
	if(ifree>=MAXVARS/2){
		Globalvarcnt++;
		if(Globalvarcnt>=MAXVARS)error("Not enough Global variable memory");
	} else {
		Localvarcnt++;
		if(Localvarcnt>=MAXVARS)error("Not enough Local variable memory");
	}
	varcnt=Globalvarcnt+Localvarcnt;
    VarIndex = vindex = ifree;

    // initialise it: save the name, set the initial value to zero and set the type
    s = name;  x = vartbl[ifree].name; j = namelen;
    while(j--) *x++ = *s++;
    if(namelen < MAXVARLEN)*x++ = 0;
    vartbl[ifree].type = vtype | (action & (T_IMPLIED | T_CONST));
    if(ifree<MAXVARS/2){
    	hashlist[hashlistpointer].level=LocalIndex;
    	hashlist[hashlistpointer++].hash=ifree;
        vartbl[ifree].level = LocalIndex;
    } else vartbl[ifree].level = 0;
//    cleardims(&vartbl[ifree].dims[0]);
    for(j = 0; j < MAXDIM; j++) vartbl[ifree].dims[j] = 0;
//    MMPrintString("Creating variable : ");MMPrintString(vartbl[ifree].name);MMPrintString(", at depth : ");PInt(vartbl[ifree].level);MMPrintString(", hash key : ");PInt(ifree);PRet();
    // the easy request is for is a non array numeric variable, so just initialise to
    // zero and return the pointer
    if(dnbr == 0) {
        if(vtype & T_NBR) {
            vartbl[ifree].val.f = 0;
            return &(vartbl[ifree].val.f);
        } else if(vtype & T_INT) {
            vartbl[ifree].val.i = 0;
            return &(vartbl[ifree].val.i);
        }
    }

    // if this is a definition of an empty array (only used in the parameter list for a sub/function)
    if(dnbr == -1) {
        vartbl[vindex].dims[0] = -1;                                // let the caller know that this is an empty array and needs more work
        return vartbl[vindex].val.s;                                // just return a pointer to the data element as it will be replaced in the sub/fun with a pointer
    }

    // if this is an array copy the array dimensions and calculate the overall size
    // for a non array string this will leave nbr = 1 which is just what we want
    for(nbr = 1, i = 0; i < dnbr; i++) {
        if(dim[i] <= OptionBase) error("Dimensions");
        vartbl[vindex].dims[i] = dim[i];
        nbr *= (dim[i] + 1 - OptionBase);
    }

    // we now have a string, an array of strings or an array of numbers
    // all need some memory to be allocated (note: GetMemory() zeros the memory)

    // First, set the important characteristics of the variable to indicate that the
    // variable is not allocated.  Thus, if GetMemory() fails with "not enough memory",
    // the variable will remain not allocated
    vartbl[ifree].val.s = NULL;
    vartbl[ifree].type = T_BLOCKED;
    i = *vartbl[ifree].name;   *vartbl[ifree].name = 0;
	j = vartbl[ifree].dims[0]; vartbl[ifree].dims[0] = 0;


	// Now, grab the memory
    if(vtype & (T_NBR | T_INT)) {
    	tmp=(nbr * sizeof(MMFLOAT));
        if(tmp<=256)mptr = GetMemory(STRINGSIZE);
        else mptr = GetMemory(tmp);
    }  else {
    	tmp=(nbr * (size + 1));
    	if(tmp<=16 && j==0)mptr = (void *)&vartbl[ifree].dims[1];
    	else if(tmp<=12 && vartbl[ifree].dims[1]==0)mptr = (void *)&vartbl[ifree].dims[2];
    	else if(tmp<=256)mptr = GetMemory(STRINGSIZE);
        else mptr = GetMemory(tmp);
    }


    // If we reached here the memory request was successful, so restore the details of
    // the variable that were saved previously and set the variables pointer to the
    // allocated memory
    vartbl[ifree].type = vtype | (action & (T_IMPLIED | T_CONST));
    *vartbl[ifree].name = i;
    vartbl[ifree].dims[0] = j;
    vartbl[ifree].size = size;
    vartbl[ifree].val.s = mptr;
    return mptr;
}



/********************************************************************************************************************************************
 utility routines
 these routines form a library of functions that any command or function can use when dealing with its arguments
 by centralising these routines it is hoped that bugs can be more easily found and corrected (unlike bwBasic !)
*********************************************************************************************************************************************/

// take a line of basic code and split it into arguments
// this function should always be called via the macro getargs
//
// a new argument is created by any of the chars in the string delim (not in brackets or quotes)
// with this function commands have much less work to do to evaluate the arguments
//
// The arguments are:
//   pointer to a pointer which points to the string to be broken into arguments.
//   the maximum number of arguments that are expected.  an error will be thrown if more than this are found.
//   buffer where the returned strings are to be stored
//   pointer to an array of strings that will contain (after the function has returned) the values of each argument
//   pointer to an integer that will contain (after the function has returned) the number of arguments found
//   pointer to a string that contains the characters to be used in spliting up the line.  If the first unsigned char of that
//       string is an opening bracket '(' this function will expect the arg list to be enclosed in brackets.
void __not_in_flash_func(makeargs)(unsigned char **p, int maxargs, unsigned char *argbuf, unsigned char *argv[], int *argc, unsigned char *delim) {
    unsigned char *op;
    int inarg, expect_cmd, expect_bracket, then_tkn, else_tkn;
    unsigned char *tp;

    TestStackOverflow();                                            // throw an error if we have overflowed the PIC32's stack

    tp = *p;
    op = argbuf;
    *argc = 0;
    inarg = false;
    expect_cmd = false;
    expect_bracket = false;
    then_tkn = tokenTHEN;
    else_tkn = tokenELSE;

    // skip leading spaces
    while(*tp == ' ') tp++;

    // check if we are processing a list enclosed in brackets and if so
    //  - skip the opening bracket
    //  - flag that a closing bracket should be found
    if(*delim == '(') {
        if(*tp != '(')
            error("Syntax");
        expect_bracket = true;
        delim++;
        tp++;
    }

    // the main processing loop
    while(*tp) {

        if(expect_bracket == true && *tp == ')') break;

        // comment char causes the rest of the line to be skipped
        if(*tp == '\'') {
            break;
        }

        // the special characters that cause the line to be split up are in the string delim
        // any other chars form part of the one argument
        if(strchr((char *)delim, (char)*tp) != NULL && !expect_cmd) {
            if(*tp == then_tkn || *tp == else_tkn) expect_cmd = true;
            if(inarg) {                                             // if we have been processing an argument
                while(op > argbuf && *(op - 1) == ' ') op--;        // trim trailing spaces
                *op++ = 0;                                          // terminate it
            } else if(*argc) {                                      // otherwise we have two delimiters in a row (except for the first argument)
                argv[(*argc)++] = op;                               // create a null argument to go between the two delimiters
                *op++ = 0;                                          // and terminate it
            }

            inarg = false;
            if(*argc >= maxargs) error("Syntax");
            argv[(*argc)++] = op;                                   // save the pointer for this delimiter
            *op++ = *tp++;                                          // copy the token or char (always one)
            *op++ = 0;                                              // terminate it
            continue;
        }

        // check if we have a THEN or ELSE token and if so flag that a command should be next
        if(*tp == then_tkn || *tp == else_tkn) expect_cmd = true;


        // remove all spaces (outside of quoted text and bracketed text)
        if(!inarg && *tp == ' ') {
            tp++;
            continue;
        }

        // not a special char so we must start a new argument
        if(!inarg) {
            if(*argc >= maxargs) error("Syntax");
            argv[(*argc)++] = op;                                   // save the pointer for this arg
            inarg = true;
        }

        // if an opening bracket '(' copy everything until we hit the matching closing bracket
        // this includes special characters such as , and ; and keeps track of any nested brackets
        if(*tp == '(' || ((tokentype(*tp) & T_FUN) && !expect_cmd)) {
            int x;
            x = (getclosebracket(tp) - tp) + 1;
            memcpy(op, tp, x);
            op += x; tp += x;
            continue;
        }

        // if quote mark (") copy everything until the closing quote
        // this includes special characters such as , and ;
        // the tokenise() function will have ensured that the closing quote is always there
        if(*tp == '"') {
            do {
                *op++ = *tp++;
                if(*tp == 0) error("Syntax");
            } while(*tp != '"');
            *op++ = *tp++;
            continue;
        }

        // anything else is just copied into the argument
        *op++ = *tp++;

        expect_cmd = false;
    }
    if(expect_bracket && *tp != ')') error("Syntax");
    while(op - 1 > argbuf && *(op-1) == ' ') --op;                  // trim any trailing spaces on the last argument
    *op = 0;                                                        // terminate the last argument
}


// throw an error
// displays the error message and aborts the program
// the message can contain variable text which is indicated by a special character in the message string
//  $ = insert a string at this place
//  @ = insert a character
//  % = insert a number
// the optional data to be inserted is the second argument to this function
// this uses longjump to skip back to the command input and cleanup the stack
void error(char *msg, ...) {
    char *p, *tp, tstr[STRINGSIZE * 2];
    va_list ap;
    
    // first build the error message in the global string MMErrMsg
    if(MMerrno == 0) MMerrno = 16;                                  // indicate an error
    memset(tstr, 0, STRINGSIZE * 2);                                 // clear any previous string
    if(*msg) {
        va_start(ap, msg);
        while(*msg) {
            tp = &tstr[strlen(tstr)];                               // point to the end of the string
            if(*msg == '$')                                         // insert a string
                strcpy(tp, va_arg(ap, char *));
            else if(*msg == '@')                                    // insert a character
                *tp = (va_arg(ap, int));
            else if(*msg == '%')                                    // insert an integer
                IntToStr(tp, va_arg(ap, int), 10);
            else
                *tp = *msg;
            msg++;
        }
    }
    
    // copy the error message into the global MMErrMsg truncating at any tokens or if the string is too long
    for(p = MMErrMsg, tp = tstr; *tp < 127 && (tp - tstr) < MAXERRMSG - 1; ) *p++ = *tp++;
    *p = 0;
    
    if(OptionErrorSkip) longjmp(ErrNext, 1);                       // if OPTION ERROR SKIP/IGNORE is in force
    if(MMCharPos > 1) MMPrintString("\r\n");
    if(CurrentLinePtr) {
        tp = p = ProgMemory;
        if(*CurrentLinePtr != T_NEWLINE && CurrentLinePtr < ProgMemory + Option.PROG_FLASH_SIZE) {
            // normally CurrentLinePtr points to a T_NEWLINE token but in this case it does not
            // so we have to search for the start of the line and set CurrentLinePtr to that
          while(*p != 0xff) {
              while(*p) p++;                                        // look for the zero marking the start of an element
              if(p >= (char *)CurrentLinePtr || p[1] == 0) {                // the previous line was the one that we wanted
                  CurrentLinePtr = tp;
                  break;
                }
              if(p[1] == T_NEWLINE) {
                  tp = ++p;                                         // save because it might be the line we want
              }
         		p++;                                                // step over the zero marking the start of the element
        		skipspace(p);
        		if(p[0] == T_LABEL) p += p[1] + 2;					// skip over the label
            }
        }

       // we now have CurrentLinePtr pointing to the start of the line
//        dump(CurrentLinePtr, 80);
        llist(tknbuf, CurrentLinePtr);
        p = tknbuf; skipspace(p);
        MMPrintString(MMCharPos > 1 ? "\r\n[" : "[");
        if(CurrentLinePtr < ProgMemory + Option.PROG_FLASH_SIZE) {
            IntToStr(inpbuf, CountLines(CurrentLinePtr), 10);
            MMPrintString(inpbuf);
            StartEditPoint = CurrentLinePtr;
            StartEditChar = 0;
        } else
            MMPrintString("LIBRARY");
        MMPrintString("] ");
        MMPrintString(p);
        MMPrintString("\r\n");
    }
    MMPrintString("Error");
    if(*MMErrMsg) {
        MMPrintString(" : ");
        MMPrintString(MMErrMsg);
    }
    MMPrintString("\r\n");
    memset(inpbuf,0,STRINGSIZE);
    longjmp(mark, 1);
}



/**********************************************************************************************
 Routines to convert floats and integers to formatted strings
 These replace the sprintf() libraries with much less flash usage
**********************************************************************************************/

#define IntToStrBufSize 65

// convert a integer to a string.
// sstr is a buffer where the chars are to be written to
// sum is the number to be converted
// base is the numbers base radix (10 = decimal, 16 = hex, etc)
// if base 10 the number will be signed otherwise it will be unsigned
void IntToStr(char *strr, long long int nbr, unsigned int base) {
    int i, negative;
    unsigned char digit;
    unsigned long long int sum;
    extern long long int llabs (long long int n);

    unsigned char str[IntToStrBufSize];

    if(nbr < 0 && base == 10) {                                     // we can have negative numbers in base 10 only
        nbr = llabs(nbr);
        negative = true;
    } else
        negative = false;

    // this generates the digits in reverse order
    sum = (unsigned long long int) nbr;
    i = 0;
    do {
        digit = sum % base;
        if (digit < 0xA)
            str[i++] = '0' + digit;
        else
            str[i++] = 'A' + digit - 0xA;
        sum /= base;
    } while (sum && i < IntToStrBufSize);

    if(negative) *strr++ = '-';

    // we now need to reverse the digits into their correct order
    for(i--; i >= 0; i--) *strr++ = str[i];
    *strr = 0;
}


// convert an integer to a string padded with a leading character
// p is a pointer to the destination
// nbr is the number to convert (can be signed in which case the number is preceeded by '-')
// padch is the leading padding char (usually a space)
// maxch is the desired width of the resultant string (incl padding chars)
// radix is the base of the number.  Base 10 is signed, all others are unsigned
// Special case (used by FloatToStr() only):
//     if padch is negative and nbr is zero prefix the number with the - sign
void IntToStrPad(char *p, long long int nbr, signed char padch, int maxch, int radix) {
    int j;
    char sign, buf[IntToStrBufSize];

    sign = 0;
    if((nbr < 0 && radix == 10)|| padch < 0) {                      // if the number is negative or we are forced to use a - symbol
        sign = '-';                                                 // set the sign
        nbr *= -1;                                                  // convert to a positive nbr
        padch = abs(padch);
    } else {
        if(nbr >= 0 && maxch < 0 && radix == 10)                    // should we display the + sign?
            sign = '+';
    }

    IntToStr(buf, nbr, radix);
    j = abs(maxch) - strlen(buf);                                   // calc padding required
    if(j <= 0)j = 0;
    else memset(p, padch, abs(maxch));                               // fill the buffer with the padding char
	if(sign != 0) {                                                 // if we need a sign
		if(j == 0) j = 1;                                           // make space if necessary
	    if(padch == '0')
	        p[0] = sign;                                            // for 0 padding the sign is before the padding
	    else
	        p[j - 1] = sign;                                        // for anything else the padding is before the sign
	 }
    strcpy(&p[j], buf) ;
}


// convert a float to a string including scientific notation if necessary
// p is the buffer to store the string
// f is the number
// m is the nbr of chars before the decimal point (if negative print the + sign)
// n is the nbr chars after the point
//     if n == STR_AUTO_PRECISION we should automatically determine the precision
//     if n is negative always use exponential format
// ch is the leading pad char
void FloatToStr(char *p, MMFLOAT f, int m, int n, unsigned char ch) {
    int exp, trim = false, digit;
    MMFLOAT rounding;
    char *pp;

    ch &= 0x7f;                                                     // make sure that ch is an ASCII char
    if(f == 0)
        exp = 0;
    else
        exp = floor(log10(fabs(f)));                             // get the exponent part
    if(((fabs(f) < 0.0001 || fabs(f) >= 1000000) && f != 0 && n == STR_AUTO_PRECISION) || n < 0) {
        // we must use scientific notation
        f /= pow(10, exp);                                         // scale the number to 1.2345
        if(f >= 10) { f /= 10; exp++; }
        if(n < 0) n = -n;                                           // negative indicates always use exponantial format
        FloatToStr(p, f, m, n, ch);                                 // recursively call ourself to convert that to a string
        p = p + strlen(p);
        *p++ = 'e';                                                 // add the exponent
        if(exp >= 0) {
            *p++ = '+';
            IntToStrPad(p, exp, '0', 2, 10);                        // add a positive exponent
        } else {
            *p++ = '-';
            IntToStrPad(p, exp * -1, '0', 2, 10);                   // add a negative exponent
        }
    } else {
        // we can treat it as a normal number

        // first figure out how many decimal places we want.
        // n == STR_AUTO_PRECISION means that we should automatically determine the precision
        if(n == STR_AUTO_PRECISION) {
            trim = true;
            n = STR_SIG_DIGITS - exp;
            if(n < 0) n = 0;
        }

        // calculate rounding to hide the vagaries of floating point
        if(n > 0)
            rounding = 0.5/pow(10, n);
        else
            rounding = 0.5;
        if(f > 0) f += rounding;                                    // add rounding for positive numbers
        if(f < 0) f -= rounding;                                    // add rounding for negative numbers

        // convert the digits before the decimal point
        if((int)f == 0 && f < 0)
            IntToStrPad(p, 0, -ch, m, 10);                          // convert -0 incl padding if necessary
        else
            IntToStrPad(p, f, ch, m, 10);                           // convert the integer incl padding if necessary
        p += strlen(p);                                             // point to the end of the integer
        pp = p;

        // convert the digits after the decimal point
        if(f < 0) f = -f;                                           // make the number positive
        if(n > 0) {                                                 // if we need to have a decimal point and following digits
            *pp++ = '.';                                            // add the decimal point
            f -= floor(f);                                         // get just the fractional part
            while(n--) {
                f *= 10;
                digit = floor(f);                                  // get the next digit for the string
                f -= digit;
                *pp++ = digit + '0';
            }

            // if we do not have a fixed number of decimal places step backwards removing trailing zeros and the decimal point if necessary
            while(trim && pp > p) {
                pp--;
                if(*pp == '.') break;
                if(*pp != '0') { pp++; break; }
            }
        }
        *pp = 0;
    }
}



/**********************************************************************************************
Various routines to clear memory or the interpreter's state
**********************************************************************************************/


// clear (or delete) variables
// if level is not zero it will only delete local variables at that level or greater
// if level is zero to will delete all variables and reset global settings
void __not_in_flash_func(ClearVars)(int level) {
   int i, newhashpointer,hashcurrent,hashnext;

    // first step through the variable table and delete local variables at that level or greater
	if(level){
		newhashpointer=hashlistpointer; //save the current number of stored values
		for(i=hashlistpointer-1;i>=0;i--){ //delete in reverse order of creation
			if(hashlist[i].level>= level){
				hashnext = hashcurrent = hashlist[i].hash;
				hashnext++;
				hashnext %= MAXVARS/2;
				if(((vartbl[hashcurrent].type & T_STR) || vartbl[hashcurrent].dims[0] != 0) && !(vartbl[hashcurrent].type & T_PTR) && ((uint32_t)vartbl[hashcurrent].val.s<(uint32_t)MMHeap + Option.HEAP_SIZE)&& ((uint32_t)vartbl[hashcurrent].val.s>(uint32_t)MMHeap)) {
					FreeMemory(vartbl[hashcurrent].val.s);
                    vartbl[hashcurrent].val.s=NULL;
					// free any memory (if allocated)
				}
//				MMPrintString("Deleting ");MMPrintString(vartbl[hashlist[i].hash].name);PIntComma(hashlist[i].level);PIntComma(hashlist[i].hash);PRet();
				hashlist[i].level=-1;
				newhashpointer=i; //set the new highest index
				memset(&vartbl[hashcurrent],0,sizeof(struct s_vartbl));
				if(vartbl[hashnext].type){
					vartbl[hashcurrent].type = T_BLOCKED ;                                // block slot
					vartbl[hashcurrent].name[0] = '~';                                      // safety precaution
				}
				Localvarcnt--;
			}
		}
		hashlistpointer=newhashpointer;
	} else {
		for(i = 0; i < MAXVARS; i++) {
			if(((vartbl[i].type & T_STR) || vartbl[i].dims[0] != 0) && !(vartbl[i].type & T_PTR)) {
				if((uint32_t)vartbl[i].val.s>(uint32_t)MMHeap && (uint32_t)vartbl[hashcurrent].val.s<(uint32_t)MMHeap + Option.HEAP_SIZE){
                    FreeMemory(vartbl[i].val.s);                        // free any memory (if allocated)
                    vartbl[i].val.s=NULL;
                }
			}
			memset(&vartbl[i],0,sizeof(struct s_vartbl));
		}
	}
   // then step through the for...next table and remove any loops at the level or greater
    for(i = 0; i < forindex; i++) {
        if(forstack[i].level >= level) {
            forindex = i;
            break;
        }
    }

    // also step through the do...loop table and remove any loops at the level or greater
    for(i = 0; i < doindex; i++) {
        if(dostack[i].level >= level) {
            doindex = i;
            break;
        }
    }

    if(level != 0) return;

    forindex = doindex = 0;
    LocalIndex = 0;                                                 // signal that all space is to be cleared
    ClearTempMemory();                                              // clear temp string space
    // we can now delete all variables by zeroing the counters
    Localvarcnt = 0;
    Globalvarcnt = 0;
    OptionBase = 0;
    DimUsed = false;
    hashlistpointer=0;
}


// clear all stack pointers (eg, FOR/NEXT stack, DO/LOOP stack, GOSUB stack, etc)
// this is done at the command prompt or at any break
void  ClearStack(void) {
    NextData = 0;
	NextDataLine = ProgMemory;
    forindex = 0;
    doindex = 0;
    gosubindex = 0;
    LocalIndex = 0;
    TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
    InterruptReturn = NULL;
}


// clear the runtime (eg, variables, external I/O, etc) includes ClearStack() and ClearVars()
// this is done before running a program
void ClearRuntime(void) {
    int i;
    ClearExternalIO();                                              // this MUST come before InitHeap()
    ClearStack();
    OptionExplicit = false;
    DefaultType = T_NBR;
    CloseAllFiles();
    findlabel(NULL);                                                // clear the label cache
    OptionErrorSkip = 0;
    MMerrno = 0;                                                    // clear the error flags
    *MMErrMsg = 0;
	#if defined(MMFAMILY) || defined(DOS)
	    NbrModules = 0;
    #endif
    InitHeap();
    m_alloc(M_VAR);
    ClearVars(0);
    varcnt = 0;
    CurrentLinePtr = ContinuePoint = NULL;
    for(i = 0;  i < MAXSUBFUN; i++)  subfun[i] = NULL;
    for(i = 1; i < Option.MaxCtrls; i++) {
        memset(&Ctrl[i],0,sizeof(struct s_ctrl));
        Ctrl[i].state = Ctrl[i].type = 0;
        Ctrl[i].s = NULL;
    }
}



// clear everything including program memory (includes ClearStack() and ClearRuntime())
// this is used before loading a program
void ClearProgram(void) {
//    InitHeap();
    m_alloc(M_PROG);                                           // init the variables for program memory
    ClearRuntime();
//    ProgMemory[0] = ProgMemory[1] = ProgMemory[3] = ProgMemory[4] = 0;
    PSize = 0;
    autoOn = 0; autoNext = 10; autoIncr = 10;                       // use by the AUTO command
    StartEditPoint = NULL;
    StartEditChar= 0;
    ProgramChanged = false;
    TraceOn = false;
}




// round a float to an integer
int __not_in_flash_func(FloatToInt32)(MMFLOAT x) {
    if(x < LONG_MIN - 0.5 || x > LONG_MAX + 0.5)
        error("Number too large");
    return (x >= 0 ? (int)(x + 0.5) : (int)(x - 0.5)) ;
}



long long int  __not_in_flash_func(FloatToInt64)(MMFLOAT x) {
    if(x < (-(0x7fffffffffffffffLL) -1) - 0.5 || x > 0x7fffffffffffffffLL + 0.5)
        error("Number too large");
    if ((x < -0xfffffffffffff) || (x > 0xfffffffffffff))
        return (long long int)(x);
    else
    return (x >= 0 ? (long long int )(x + 0.5) : (long long int )(x - 0.5)) ;
}



// make a string uppercase
void __not_in_flash_func(makeupper)(unsigned char *p) {
    while(*p) {
        *p = toupper(*p);
        p++;
    }
}


// find the value of a command token given its name
int GetCommandValue( unsigned char *n) {
    int i;
    for(i = 0; i < CommandTableSize - 1; i++)
        if(str_equal(n, commandtbl[i].name))
            return i + C_BASETOKEN;
    error("Internal fault (sorry)");
    return 0;
}



// find the value of a token given its name
int GetTokenValue (unsigned char *n) {
    int i;
    for(i = 0; i < TokenTableSize - 1; i++)
        if(str_equal(n, tokentbl[i].name))
            return i + C_BASETOKEN;
    error("Internal fault (sorry)");
    return 0;
}



// skip to the end of a variable
unsigned char __not_in_flash_func(*skipvar)(unsigned char *p, int noerror) {
    unsigned char *pp, *tp;
    int i;
    int inquote = false;

    tp = p;
    // check the first char for a legal variable name
    skipspace(p);
    if(!isnamestart(*p)) return tp;

    do {
        p++;
    } while(isnamechar(*p));

    // check the terminating char.
    if(*p == '$' || *p == '%' || *p == '!') p++;

    if(p - tp > MAXVARLEN) {
        if(noerror) return p;
        error("Variable name too long");
    }

    pp = p; skipspace(pp); if(*pp == (unsigned char)'(') p = pp;
    if(*p == '(') {
        // this is an array

        p++;
        if(p - tp > MAXVARLEN) {
            if(noerror) return p;
            error("Variable name too long");
        }

        // step over the parameters keeping track of nested brackets
        i = 1;
        while(1) {
            if(*p == '\"') inquote = !inquote;
            if(*p == 0) {
                if(noerror) return p;
                error("Expected closing bracket");
            }
            if(!inquote) {
                if(*p == ')') if(--i == 0) break;
                if(*p == '(' || (tokentype(*p) & T_FUN)) i++;
            }
            p++;
        }
        p++;        // step over the closing bracket
    }
    return p;
}



// skip to the end of an expression (terminates on null, comma, comment or unpaired ')'
unsigned char __not_in_flash_func(*skipexpression)(unsigned char *p) {
    int i, inquote;

    for(i = inquote = 0; *p; p++) {
        if(*p == '\"') inquote = !inquote;
        if(!inquote) {
            if(*p == ')') i--;
            if(*p == '(' || (tokentype(*p) & T_FUN)) i++;
        }
        if(i < 0 || (i == 0 && (*p == ',' || *p == '\''))) break;
    }
    return p;
}


// find the next command in the program
// this contains the logic for stepping over a line number and label (if present)
// p is the current place in the program to start the search from
// CLine is a pointer to a char pointer which in turn points to the start of the current line for error reporting (if NULL it will be ignored)
// EOFMsg is the error message to use if the end of the program is reached
// returns a pointer to the next command
unsigned char __not_in_flash_func(*GetNextCommand)(unsigned char *p, unsigned char **CLine, unsigned char *EOFMsg) {
    do {
        if(*p != T_NEWLINE) {                                       // if we are not already at the start of a line
            while(*p) p++;                                          // look for the zero marking the start of an element
            p++;                                                    // step over the zero
        }
        if(*p == 0) {
            if(EOFMsg == NULL) return p;
            error(EOFMsg);
        }
        if(*p == T_NEWLINE) {
            if(CLine) *CLine = p;                                   // and a pointer to the line also for error reporting
            p++;
        }
        if(*p == T_LINENBR) p += 3;

        skipspace(p);
        if(p[0] == T_LABEL) {                                       // got a label
            p += p[1] + 2;                                          // skip over the label
            skipspace(p);                                           // and any following spaces
        }
    } while(*p < C_BASETOKEN);
    return p;
}


// scans text looking for the matching closing bracket
// it will handle nested strings, brackets and functions
// it expects to be called pointing at the opening bracket or a function token
unsigned char __not_in_flash_func(*getclosebracket)(unsigned char *p) {
    int i = 0;
    int inquote = false;

    do {
        if(*p == 0) error("Expected closing bracket");
        if(*p == '\"') inquote = !inquote;
        if(!inquote) {
            if(*p == ')') i--;
            if(*p == '(' || (tokentype(*p) & T_FUN)) i++;
        }
        p++;
    } while(i);
    return p - 1;
}


// check that there is no excess text following an element
// will skip spaces and abort if a zero char is not found
void __not_in_flash_func(checkend)(unsigned char *p) {
    skipspace(p);
    if(*p == '\'') return;
    if(*p)
        error("Unexpected text: $", p);
}


// check if the next text in an element (a basic statement) corresponds to an alpha string
// leading whitespace is skipped and the string must be terminated with a valid terminating
// character. Returns a pointer to the end of the string if found or NULL is not
unsigned char __not_in_flash_func(*checkstring)(unsigned char *p, unsigned char *tkn) {
    skipspace(p);                                           // skip leading spaces
    while(*tkn && (toupper(*tkn) == toupper(*p))) { tkn++; p++; }   // compare the strings
    if(*tkn == 0 && (*p == (unsigned char)' ' || *p == (unsigned char)',' || *p == (unsigned char)'\'' || *p == 0|| *p == (unsigned char)'(' )) {
        skipspace(p);
        return p;                                                   // if successful return a pointer to the next non space character after the matched string
    }
    return NULL;                                                    // or NULL if not
}

/********************************************************************************************************************************************
A couple of I/O routines that do not belong anywhere else
*********************************************************************************************************************************************/





/********************************************************************************************************************************************
 string routines
 these routines form a library of functions for manipulating MMBasic strings.  These strings differ from ordinary C strings in that the length
 of the string is stored in the first byte and the string is NOT terminated with a zero valued byte.  This type of string can store the full
 range of binary values (0x00 to 0xff) in each character.
*********************************************************************************************************************************************/

// convert a MMBasic string to a C style string
// if the MMstr contains a null byte that byte is skipped and not copied
unsigned char __not_in_flash_func(*MtoC)(unsigned char *p) {
    int i;
    unsigned char *p1, *p2;
    i = *p;
    p1 = p + 1; p2 = p;
    while(i) {
        if(p1) *p2++ = *p1;
        p1++;
        i--;
    }
    *p2 = 0;
    return p;
}


// convert a c style string to a MMBasic string
unsigned char __not_in_flash_func(*CtoM)(unsigned char *p) {
    int len, i;
    unsigned char *p1, *p2;
    len = i = strlen((char *)p);
    if(len > MAXSTRLEN) error("String too long");
    p1 = p + len; p2 = p + len - 1;
    while(i--) *p1-- = *p2--;
    *p = len;
    return p;
}


// copy a MMBasic string to a new location
void __not_in_flash_func(Mstrcpy)(unsigned char *dest, unsigned char *src) {
    int i;
    i = *src + 1;
    while(i--) *dest++ = *src++;
}



// concatenate two MMBasic strings
void __not_in_flash_func(Mstrcat)(unsigned char *dest, unsigned char *src) {
    int i;
    i = *src;
    *dest += i;
    dest += *dest + 1 - i; src++;
    while(i--) *dest++ = *src++;
}



// compare two MMBasic style strings
// returns 1 if s1 > s2  or  0 if s1 = s2  or  -1 if s1 < s2
int __not_in_flash_func(Mstrcmp)(unsigned char *s1, unsigned char *s2) {
    register int i;
    register unsigned char *p1, *p2;

    // get the smaller length
    i = *s1 < *s2 ? *s1 : *s2;

    // skip the length byte and point to the unsigned char array
    p1 = s1 + 1; p2 = s2 + 1;

    // compare each char
    while(i--) {
        if(*p1 > *p2) return 1;
        if(*p1 < *p2) return -1;
        p1++; p2++;
    }
    // up to this point the strings matched - make the decision based on which one is shorter
    if(*s1 > *s2) return 1;
    if(*s1 < *s2) return -1;
    return 0;
}



////////////////////////////////////////////////////////////////////////////////////////////////////
// these library functions went missing in the PIC32 C compiler ver 1.12 and later
////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * mystrncasecmp.c --
 *
 *  Source code for the "mystrncasecmp" library routine.
 *
 * Copyright (c) 1988-1993 The Regents of the University of California.
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: mystrncasecmp.c,v 1.3 2007/04/16 13:36:34 dkf Exp $
 */

/*
 * This array is designed for mapping upper and lower case letter together for
 * a case independent comparison. The mappings are based upon ASCII character
 * sequences.
 */

static unsigned char charmap[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xe1, 0xe2, 0xe3, 0xe4, 0xc5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};


/*
 *----------------------------------------------------------------------
 *
 * mystrncasecmp --
 *
 *  Compares two strings, ignoring case differences.
 *
 * Results:
 *  Compares up to length chars of s1 and s2, returning -1, 0, or 1 if s1
 *  is lexicographically less than, equal to, or greater than s2 over
 *  those characters.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */


int __not_in_flash_func(mystrncasecmp)(
    const unsigned char *s1,         /* First string. */
    const unsigned char *s2,         /* Second string. */
    size_t  length)      /* Maximum number of characters to compare
                         * (stop earlier if the end of either string
                         * is reached). */
{
    register unsigned char u1, u2;

    for (; length != 0; length--, s1++, s2++) {
        u1 = (unsigned char) *s1;
        u2 = (unsigned char) *s2;
        if (charmap[u1] != charmap[u2]) {
            return charmap[u1] - charmap[u2];
        }
        if (u1 == '\0') {
            return 0;
        }
    }
    return 0;
}



// Compare two strings, ignoring case differences.
// Returns true if the strings are equal (ignoring case) otherwise returns false.
#if defined(__PIC32MX__)
inline
#endif
int __not_in_flash_func(str_equal)(const unsigned char *s1, const unsigned char *s2) {
    if(charmap[*(unsigned char *)s1] != charmap[*(unsigned char *)s2]) return 0;
    for ( ; ; ) {
        if(*s2 == '\0') return 1;
        s1++; s2++;
        if(charmap[*(unsigned char *)s1] != charmap[*(unsigned char *)s2]) return 0;
    }
    return 0;
}


// Compare two areas of memory, ignoring case differences.
// Returns true if they are equal (ignoring case) otherwise returns false.
int __not_in_flash_func(mem_equal)(unsigned char *s1, unsigned char *s2, int i) {
    if(charmap[*(unsigned char *)s1] != charmap[*(unsigned char *)s2]) return 0;
    while (--i) {
        if(charmap[*(unsigned char *)++s1] != charmap[*(unsigned char *)++s2])
            return 0;
    }
    return 1;
}