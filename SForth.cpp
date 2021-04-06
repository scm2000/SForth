#include <sys/types.h>
#include <stdio.h>
#include <setjmp.h>
#include <Arduino.h>

//#define DEBUG

#include "SForth.h"
#include "utils.h"
#include "CompilationBuffer.h"
#include "Dictionary.h"

CompilationBuffer compilationBuffer;
Dictionary dictionary;

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
  DEBUG_PRINT("dStackPushed was in fact called");
#ifdef DEBUG
  outputFormat("val was: %d%s", val, eol);
#endif

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
    outputFormat("Scan state: %s%s", cptr, eol);
#endif

    if (tptr - curToken < MAX_TOKEN_LEN)
    {
      *tptr++ = *cptr;
    }
    ++cptr;
  }

  *tptr = '\0';
#ifdef DEBUG
  outputFormat("FinalToken: %s%s", curToken, eol);
#endif
}

//////////// predefined ops
static void printUnsignedDecimalValue()
{
  uint32_t val = dStackPop();
  outputFormat("%u%s", val, eol);
}
static void printSignedDecimalValue()
{
  uint32_t val = dStackPop();
  outputFormat("%d%s", val, eol);
}
static void printHexValue()
{
  uint32_t val = dStackPop();
  outputFormat("0x%08x%s", val, eol);
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
    dictionary.define(curToken, Dictionary::variable, 0);
  }
  else
  {
    myThrow (notAWord, "non-existent or numeric token for variable");
  }
}


static uint32_t tokenToNumber();

static void sfDefineFunction()
{
  // grab tokens up to a ; and compile to machine code, woo hoo!

  // function prolog is to push {r3, lr}
  compilationBuffer.beginFunction();

  nextToken(); // better be the word we are defining
  if (!isalpha(curToken[0]))
    myThrow(notAWord, ": defined word must start with a letter");

  char name[33];
  strcpy(name, curToken);

  nextToken();

  while (curToken[0] && curToken[0] != ';')
  {
    if (isdigit(curToken[0]))
    {
      uint32_t num = tokenToNumber();
      compilationBuffer.insertCallToVoidWithArg((uint32_t)&dStackPush, num);
    }
    else // assume a word
    {
      // find it in the dictionary
      Dictionary::dictElt *entry = dictionary.lookup(curToken);
      if (entry)
      {
        // found in dict
        switch (entry->type)
        {
          case Dictionary::variable:
            {
              DEBUG_PRINT("token is a variable reference");
              uint32_t valAddr = (uint32_t)&entry->val;
              compilationBuffer.insertCallToVoidWithArg((uint32_t)&dStackPush, valAddr);
            }
            break;

          case Dictionary::function:
            {
              DEBUG_PRINT("token is a function reference");
              uint32_t offsetStart = ((uint32_t)&entry->val) | 0x1;
              compilationBuffer.insertCallToVoid(offsetStart);
            }
            break;

          case Dictionary::predefinedFunction:
            {
              DEBUG_PRINT("token is a predefined function reference");
              compilationBuffer.insertCallToVoid(entry->val);
            }
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

  // now the prolog for the function.
  compilationBuffer.endFunction();
  
#ifdef DEBUG
  outputFormat("codeStartStuff: %08x", *((uint32_t*)compilationBuffer.compiledCode));
#endif
  
  dictionary.define(name, Dictionary::function, 0, (uint32_t*)compilationBuffer.compiledCode, compilationBuffer.halfWordCount() * 2);

  compilationBuffer.freeUp();

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
  dictionary.define("+", Dictionary::predefinedFunction, sfAdd);
  dictionary.define("-", Dictionary::predefinedFunction, sfSubtract);
  dictionary.define("<<", Dictionary::predefinedFunction, sfLeftShift);
  dictionary.define(">>", Dictionary::predefinedFunction, sfRightShift);
  dictionary.define("!", Dictionary::predefinedFunction, sfStoreToMem);
  dictionary.define("@", Dictionary::predefinedFunction, sfFetchFromMem);
  dictionary.define("pinMode", Dictionary::predefinedFunction, sfPinMode);
  dictionary.define("digitalWrite", Dictionary::predefinedFunction, sfDigitalWrite);
  dictionary.define(".", Dictionary::predefinedFunction, printUnsignedDecimalValue);
  dictionary.define(".s", Dictionary::predefinedFunction, printSignedDecimalValue);
  dictionary.define(".x", Dictionary::predefinedFunction, printHexValue);
  dictionary.define("dup", Dictionary::predefinedFunction, sfDup);
  dictionary.define("swap", Dictionary::predefinedFunction, sfSwap);
  dictionary.define("variable", Dictionary::predefinedFunction, sfVariable);
  dictionary.define(":", Dictionary::predefinedFunction, sfDefineFunction);

  outputFormat("SForth is up and running!%s", eol);
#ifdef DEBUG
  // test the stack
  dStackPush(123);
  uint32_t a = dStackPop();
  if (a == 123)
    outputFormat("d stack seems ok");
  else
    outputFormat("d stack did not work");
#endif

}

static uint32_t tokenToNumber()
{
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
  return val;
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

      dStackPush(tokenToNumber());
    }
    else // must be a word
    {
      DEBUG_PRINT("token is a word");

      // assume it is a word
      // find it in the dictionary
      Dictionary::dictElt *entry = dictionary.lookup(curToken);
      if (entry)
      {
        // found in dict
        switch (entry->type)
        {
          case Dictionary::variable:
            DEBUG_PRINT("token is a variable reference");
            // push the address of the variable's numVal;
            dStackPush((uint32_t)&entry->val);
            break;

          case Dictionary::function:
            {
              DEBUG_PRINT("token is a function reference");
              // wow.. call the function code packed in the dictElt
              volatile void (*fptr)(void);

              uint32_t offsetStart = ((uint32_t)&entry->val) | 0x1;
#ifdef DEBUG
              outputFormat("fooCode: %08x%s", entry->val, eol);
              uint16_t *dumpLoc = (uint16_t*)&entry->val;
              for (int i = 0; i < 12; ++i)
              {
                outputFormat("dump %d: %04x%s", i, *dumpLoc++, eol);
              }
#endif
              fptr = (volatile void(*)(void))(offsetStart);
              fptr();
            }
            break;

          case Dictionary::predefinedFunction:
            DEBUG_PRINT("token is a predefined function reference");
            // call through the function pointer
            ((void (*)(void))entry->val)();
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
    outputFormat("SForth> ");
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
        Serial.print(eol);
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
