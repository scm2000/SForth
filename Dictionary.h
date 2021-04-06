#define MAX_TOKEN_LEN 32
class Dictionary
{
  public:
    Dictionary() :dictHead(0) {}
    
    /* Dictionary element types */
    typedef enum {variable, function, predefinedFunction} dictEltType;

    /* A dictionary element */
    // better make this an even number
    struct dictElt {
      uint8_t type;
      char name[MAX_TOKEN_LEN + 1];
      dictElt *prev;
      uint32_t val;
    };


    void define(char *name, dictEltType type, void (*funcPtr)(void) = 0, uint32_t *code = 0, int len = 0)
    {
      DEBUG_PRINT("dictDefine called");

      dictElt *newElt;
      switch (type)
      {
        case variable:
          {
            DEBUG_PRINT("defining a variable");
            newElt = (dictElt*)mallocMem(sizeof(dictElt));
            newElt->val = 0;
          }
          break;

        case predefinedFunction:
          {
            DEBUG_PRINT("defining a predefined function");
            newElt = (dictElt*)mallocMem(sizeof(dictElt));
            newElt->val = (uint32_t)funcPtr;
          }
          break;
        case function:
          {
            DEBUG_PRINT("defining user defined function");
            newElt = (dictElt*)mallocMem(sizeof(dictElt) + len - sizeof(uint32_t));
#ifdef DEBUG
            outputFormat("funcStart: %08x%s", &newElt->val, eol);
#endif
            //copy code over
            memcpy(&newElt->val, code, len);

#ifdef DEBUG
            outputFormat("stored code: %08x%s",  newElt->val, eol);
#endif
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

    dictElt *lookup(char *name)
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

  private:
    dictElt *dictHead;

};
