#include <sys/types.h>
#include <setjmp.h>
#include <Arduino.h>

#include "SForth.h"
static void (*outputString)(char *);

//#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(string) outputString("SForth DEBUG: "); outputString(string); outputString("\n");
#else
#define DEBUG_PRINT(string)
#endif

static jmp_buf errorJump;
static char *errorMessage = 0;

typedef enum {noError = 0, invalidDictEltType, cantMalloc, dStackUnderflow, internalError, undefinedWord, notAWord} errorCode;
void throw (errorCode code, char *message)
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
  outputString("Error: ");
  outputString(codeWord);
  outputString(", ");
  outputString(errorMessage);
  outputString("\n");
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

static void dictDefine(char *name, dictEltType type, uint32_t arg)
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
        newElt->val.preDefFunc = (void (*)(void))arg;
      }
      break;
    default:
      {
        DEBUG_PRINT("about to throw on default type");
        throw (invalidDictEltType, "in dictDefine");
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
      throw (dStackUnderflow, "in dStackPop");
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
    outputString("Scan state: ");
    outputString(cptr);
    outputString("\n");
#endif

    if (tptr - curToken < MAX_TOKEN_LEN)
    {
      *tptr++ = *cptr;
    }
    ++cptr;
  }

  *tptr = '\0';
#ifdef DEBUG
  outputString("FinalToken: ");
  outputString(curToken);
  outputString("\n");
#endif
}

//////////// predefined ops
static printUnsignedDecimalValue()
{
  uint32_t val = dStackPop();
  char valStr[12];
  sprintf(valStr, "%u\n", val);
  outputString(valStr);
}
static printSignedDecimalValue()
{
  uint32_t val = dStackPop();
  char valStr[12];
  sprintf(valStr, "%d\n", val);
  outputString(valStr);
}
static printHexValue()
{
  uint32_t val = dStackPop();
  char valStr[12];
  sprintf(valStr, "0x%08x\n", val);
  outputString(valStr);
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
  uint32_t *a = dStackPop();
  uint32_t b = dStackPop();
  *a = b;
}

static void sfFetchFromMem()
{
  // interpret a as an address to fetch from.
  uint32_t *a = dStackPop();
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
    throw (notAWord, "non-existent or numeric token for variable");
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

void toggleTest()
{
  sfDup();
  dStackPush(13);
  sfSwap();
  sfDigitalWrite();
  dStackPush(1);
  sfSwap();
  sfSubtract();
}

///////////////////SForth entry points
void SForthBegin(void (*outStrFunc)(char *))
{
  outputString = outStrFunc;

  DEBUG_PRINT("Got into begin");
  int errorCode;
  if (errorCode = setjmp(errorJump))
  {
    printError(errorCode);
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
  dictDefine("toggleTest", predefinedFunction, toggleTest);
  
  outputString("SForth is up and running!\n");
#ifdef DEBUG
  // test the stack
  dStackPush(123);
  uint32_t a = dStackPop();
  if (a == 123)
    outputString("d stack seems ok");
  else
    outputString("d stack did not work");
#endif

}

void SForthEvaluate(char *str)
{
  DEBUG_PRINT("Evaluate called");
  int errorCode;
  if (errorCode = setjmp(errorJump))
  {
    printError(errorCode);
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
            dStackPush(&entry->val.numVal);
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
            throw (internalError, "apparently the dictionary is trashed");
            break;
        }

      }
      else // undefined word
      {
        throw (undefinedWord, curToken);
      }

    }

    nextToken();
  }
}

SForth_t SForth = {SForthBegin, SForthEvaluate };



///////////////// Writing the dictionary and datastack to flash
