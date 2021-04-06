const char *eol = "\r\n";

static void outputFormat(char *fmt, ...)
{
  char tmpStr[1000];
  va_list valist;
  va_start(valist, fmt);
  vsnprintf(tmpStr, sizeof(tmpStr), fmt, valist);
  va_end(valist);
  Serial.print(tmpStr);
}

#ifdef DEBUG
#define DEBUG_PRINT(string) outputFormat("SForth DEBUG: %s%s", string,eol);
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
  outputFormat("Error: %s, %s%s", codeWord, errorMessage, eol);
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
