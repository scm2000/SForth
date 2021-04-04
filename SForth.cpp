#include <sys/types.h>
#include <setjmp.h>
#include <Arduino.h>

const char *eol = "\r\n";

#include "SForth.h"
static void outputString(char *str, ...)
{
  Serial.print(str);
  va_list valist;
  va_start(valist, str);
  char *nextStr = va_arg(valist, char *);
  while (nextStr)
  {
    Serial.print(nextStr);
    nextStr = va_arg(valist, char *);
  }
  va_end(valist);
}

//#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(string) outputString("SForth DEBUG: ", string,eol, 0);
#else
#define DEBUG_PRINT(string)
#endif

static jmp_buf errorJump;
static char *errorMessage = 0;

typedef enum {noError = 0, invalidDictEltType, cantMalloc, dStackUnderflow, internalError, undefinedWord, notAWord} errorCode;
void myThrow (errorCode code, char *message)
{
  errorMessage = message;
  longjmp(errorJump, code);
}

static void printError(errorCode code)
{
  char *codeWord = "unknown error";
  switch (code)
  {
    case invalidDictEltType:
      codeWord = "internal error invalid dictionary element type";
      break;
    case cantMalloc:
      codeWord = "out of malloc memory";
      break;
    case dStackUnderflow:
      codeWord = "dataStackUnderflow";
      break;
    case internalError:
      codeWord = "internalError";
      break;
    case undefinedWord:
      codeWord = "undefined word";
      break;
    case notAWord:
      codeWord = "not a word";
      break;
    default:
      codeWord = "unexpected error code";
      break;
  }
  outputString("Error: ", codeWord, ", ", errorMessage, eol, 0);
}

/* malloc stuff */
void *mallocMem(size_t len)
{
  void *res = malloc(len);
  if (res == 0)
  {
    longjmp(errorJump, cantMalloc);
  }
  return res;
}

/* Dictionary element types */
typedef enum {variable, function, predefinedFunction} dictEltType;

/* A dictionary element */
// better make this an even number
#define MAX_TOKEN_LEN 32
typedef struct dictElt dictElt;
typedef struct dictElt {
  uint8_t type;
  char name[MAX_TOKEN_LEN + 1];
  dictElt *prev;
  union {
    uint32_t numVal;
    void (*preDefFunc)(void);
    uint32_t funcStart;
  } val;
};

static dictElt *dictHead = 0;

static void dictDefine(char *name, dictEltType type, void (*funcPtr)(void))
{
  DEBUG_PRINT("dictDefine called");

  dictElt *newElt;
  switch (type)
  {
    case variable:
      {
        DEBUG_PRINT("defining a variable");
        newElt = (dictElt*)mallocMem(sizeof(dictElt));
        newElt->val.numVal = 0;
      }
      break;

    case predefinedFunction:
      {
        DEBUG_PRINT("defining a predefined function");
        newElt = (dictElt*)mallocMem(sizeof(dictElt));
        newElt->val.preDefFunc = funcPtr;
      }
      break;
    default:
      {
        DEBUG_PRINT("about to throw on default type");
        myThrow (invalidDictEltType, "in dictDefine");
      }
  }

  strncpy(newElt->name, name, MAX_TOKEN_LEN);
  newElt->prev = dictHead;
  newElt->type = type;
  dictHead = newElt;
}

dictElt *dictLookup(char *name)
{
  dictElt *eltFound = dictHead;

  while (eltFound)
  {
    if (strncmp(eltFound->name, name, 32) == 0)
    {
      return eltFound;
    }

    eltFound = eltFound->prev;
  }

  return 0;
}
////////////////////////////////////////////////////////

/// Data stack
typedef struct dStackBlock dStackBlock;
#define STACK_BLOCK_SIZE  512
struct dStackBlock
{
  dStackBlock *prev;
  uint32_t stack[STACK_BLOCK_SIZE];
};

static dStackBlock firstDStackBlock = {.prev = 0};
static dStackBlock *dStackHead = &firstDStackBlock;
static uint32_t *dStackPtr = firstDStackBlock.stack + STACK_BLOCK_SIZE;

void dStackPush(uint32_t val)
{
  if (dStackPtr == dStackHead->stack)
  {
    // overflowing this block, alloc a new one
    dStackBlock *newBlock = (dStackBlock*)mallocMem(sizeof(dStackBlock));
    newBlock->prev = dStackHead;
    dStackHead = newBlock;
    dStackPtr = newBlock->stack + STACK_BLOCK_SIZE;
  }
  *--dStackPtr = val;
}

uint32_t dStackPop()
{
  if (dStackPtr - dStackHead->stack == STACK_BLOCK_SIZE)
  {
    // underflowing current block, see if more blocks.
    // free cur block
    dStackBlock *old = dStackHead;
    dStackHead = old->prev;
    free(old);
    if (dStackHead == 0)
    {
      // true underflow
      // for recovery, reset to an empty dStack
      dStackHead = &firstDStackBlock;
      dStackPtr = firstDStackBlock.stack + STACK_BLOCK_SIZE;
      myThrow (dStackUnderflow, "in dStackPop");
    }
    dStackPtr = dStackHead->stack;
  }

  return *dStackPtr++;
}

//////////////////// Basic parsing
static char *cptr;
static char curToken[MAX_TOKEN_LEN + 1];

// takes next token from buffer.   tokens are delimited by white space
// and if a token is longer than MAX_TOKEN_LEN the extra chars are scanned but ignored
static void nextToken()
{
  char *tptr = curToken;

  // skip leading space
  while (*cptr && isspace(*cptr)) ++cptr;

  // transfer chars to token until space
  while (*cptr && !isspace(*cptr))
  {
#ifdef DEBUG
    outputString("Scan state: ", cptr, eol, 0);
#endif

    if (tptr - curToken < MAX_TOKEN_LEN)
    {
      *tptr++ = *cptr;
    }
    ++cptr;
  }

  *tptr = '\0';
#ifdef DEBUG
  outputString("FinalToken: ", curToken, eol, 0);
#endif
}

//////////// predefined ops
static void printUnsignedDecimalValue()
{
  uint32_t val = dStackPop();
  char valStr[12];
  sprintf(valStr, "%u%s", val, eol);
  outputString(valStr, 0);
}
static void printSignedDecimalValue()
{
  uint32_t val = dStackPop();
  char valStr[12];
  sprintf(valStr, "%d%s", val, eol);
  outputString(valStr, 0);
}
static void printHexValue()
{
  uint32_t val = dStackPop();
  char valStr[12];
  sprintf(valStr, "0x%08x%s", val, eol);
  outputString(valStr, 0);
}

static void sfAdd()
{
  dStackPush(dStackPop() + dStackPop());
}

static void sfSubtract()
{
  uint32_t a = dStackPop();
  uint32_t b = dStackPop();
  dStackPush(b - a);
}

static void sfLeftShift()
{
  uint32_t a = dStackPop();
  uint32_t b = dStackPop();
  dStackPush(b << a);
}

static void sfRightShift()
{
  uint32_t a = dStackPop();
  uint32_t b = dStackPop();
  dStackPush(b >> a);
}

static void sfStoreToMem()
{
  // interpret b as an address to store to.
  uint32_t *a = (uint32_t*)dStackPop();
  uint32_t b = dStackPop();
  *a = b;
}

static void sfFetchFromMem()
{
  // interpret a as an address to fetch from.
  uint32_t *a = (uint32_t*)dStackPop();
  dStackPush(*a);
}

static void sfDup()
{
  uint32_t a = dStackPop();
  dStackPush(a);
  dStackPush(a);
}
static void sfSwap()
{
  uint32_t a = dStackPop();
  uint32_t b = dStackPop();
  dStackPush(a);
  dStackPush(b);
}

static void sfVariable()
{
  // scan the next token and define it as a variable if appropriate
  nextToken();
  if (isalpha(curToken[0]))
  {
    dictDefine(curToken, variable, 0);
  }
  else
  {
    myThrow (notAWord, "non-existent or numeric token for variable");
  }
}

static void sfDefineFunction()
{
  // grab tokens up to a ; and compile to machine code, woo hoo!

}

// arduino like builtins
static void sfPinMode()
{
  uint32_t a = dStackPop();
  uint32_t b = dStackPop();
  pinMode(b, a);

}

static void sfDigitalWrite()
{
  uint32_t a = dStackPop();
  uint32_t b = dStackPop();
  digitalWrite(b, a);

}

///////////////////SForth entry points
void SForthBegin()
{
  DEBUG_PRINT("Got into begin");
  errorCode code = (errorCode)setjmp(errorJump);
  if (code)
  {
    printError(code);
    return;
  }

  // initialize the dictionary
  dictDefine("+", predefinedFunction, sfAdd);
  DEBUG_PRINT("defined plus");

  dictDefine("-", predefinedFunction, sfSubtract);
  dictDefine("<<", predefinedFunction, sfLeftShift);
  dictDefine(">>", predefinedFunction, sfRightShift);
  dictDefine("!", predefinedFunction, sfStoreToMem);
  dictDefine("@", predefinedFunction, sfFetchFromMem);
  dictDefine("pinMode", predefinedFunction, sfPinMode);
  dictDefine("digitalWrite", predefinedFunction, sfDigitalWrite);
  dictDefine(".", predefinedFunction, printUnsignedDecimalValue);
  dictDefine(".s", predefinedFunction, printSignedDecimalValue);
  dictDefine(".x", predefinedFunction, printHexValue);
  dictDefine("dup", predefinedFunction, sfDup);
  dictDefine("swap", predefinedFunction, sfSwap);
  dictDefine("variable", predefinedFunction, sfVariable);

  Serial.println("SForth is up and running!");
#ifdef DEBUG
  // test the stack
  dStackPush(123);
  uint32_t a = dStackPop();
  if (a == 123)
    outputString("d stack seems ok", 0);
  else
    outputString("d stack did not work", 0);
#endif

}

void SForthEvaluate(char *str)
{
  DEBUG_PRINT("Evaluate called");
  errorCode code  = (errorCode)setjmp(errorJump);
  if (code)
  {
    printError(code);
    return;
  }

  // for now, only immediate evaluation.
  cptr = str;

  nextToken();
  while (strlen(curToken))
  {
    // token is either a number or a word
    if (isdigit(curToken[0]))
    {
      DEBUG_PRINT("token is a number");

      // some kind of numeric string
      char *numFormat;
      if (curToken[1] == 'x')
        numFormat = "0x%x";
      else
        numFormat = "%d";

      DEBUG_PRINT("token is:");
      DEBUG_PRINT(curToken);
      uint32_t val;
      sscanf(curToken, numFormat, &val);
      dStackPush(val);
    }
    else // must be a word
    {
      DEBUG_PRINT("token is a word");

      // assume it is a word
      // find it in the dictionary
      dictElt *entry = dictLookup(curToken);
      if (entry)
      {
        // found in dict
        switch ((dictEltType)entry->type)
        {
          case variable:
            DEBUG_PRINT("token is a variable reference");
            // push the address of the variable's numVal;
            dStackPush((uint32_t)&entry->val.numVal);
            break;

          case function:
            DEBUG_PRINT("token is a function reference");
            // wow.. call the function code packed in the dictElt
            ((void (*)(void))((&entry->val.funcStart) + 1))();
            break;

          case predefinedFunction:
            DEBUG_PRINT("token is a predefined function reference");
            // call through the function pointer
            entry->val.preDefFunc();
            break;

          default:
            DEBUG_PRINT("dict is trashed?");
            myThrow (internalError, "apparently the dictionary is trashed");
            break;
        }

      }
      else // undefined word
      {
        myThrow (undefinedWord, curToken);
      }

    }

    nextToken();
  }
}

///////////////// the interractive "shell"
static char lineBuf[1000];
static char *lptr = lineBuf;
bool prompted = false;
static void shellHook() {
  if (!prompted)
  {
    outputString("SForth> ", 0);
    prompted = true;
  }

  if (Serial.available())
  {
    char c = Serial.read();
    switch (c)
    {
      case 0x7f: // delete key
        if (lptr != lineBuf)
        {
          Serial.print("\x08 \x08");
          --lptr;
        }
        break;

      case 0xd:
        Serial.print("\r\n");
        *lptr = '\0';
        SForth.evaluate(lineBuf);
        lptr = lineBuf;
        prompted = false;
        break;
        
      default:
        if (c > '\x1f' && c < '\x7f')
        {
          *lptr++ = c;
          Serial.print(c);
        }
        break;
    }
  }
}

SForth_t SForth = {SForthBegin, SForthEvaluate, shellHook};




///////////////// Writing the dictionary and datastack to flash
