#include <sys/types.h>
#include <setjmp.h>

static jmp_buf errorJump;
static char *errorMessage = 0;

typedef enum {noError=0, invalidDictEltType, cantMalloc, dStackUnderflow} errorCode;
void throw(errorCode code, char *message)
{
  errorMessage = message;
  longjmp(errorJump, code);
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
typedef struct dictElt dictElt;
typedef struct dictElt {
  dictEltType type;
  char name[33]; // odd to keep even alighnment for remaining fields.
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
  dictElt *newElt;
  switch (type)
  {
    case variable:
    {
      newElt = (dictElt*)mallocMem(sizeof(dictElt));
      newElt->val.numVal = 0;
    }
    break;

    case predefinedFunction:
    {
      newElt = (dictElt*)mallocMem(sizeof(dictElt));
      newElt->val.preDefFunc = (void (*)(void))arg;
    }
    default:
    {
      throw(invalidDictEltType, "in dictDefine");
    }
  }

  strncpy(newElt->name, name, 32);
  newElt->prev = dictHead;
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
      throw(dStackUnderflow, "in dStackPop");
    }
    dStackPtr = dStackHead->stack;
  }

  return *dStackPtr++;
}
