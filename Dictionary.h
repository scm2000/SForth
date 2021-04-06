#define MAX_TOKEN_LEN 32
class Dictionary
{
  public:
    Dictionary() : dictHead(0) {}

    /* Dictionary element types */
    typedef enum {variable, function, predefinedFunction, compilingFunction} dictEltType;

    /* A dictionary element */
    // make this so that val starts on an even address
    // assumping type starts on an evenAddress
    struct dictElt {
      dictEltType type;
      char name[MAX_TOKEN_LEN + 1];
      dictElt *prev;
      uint32_t val;
      void (*compFunc)(void); // for compilationFunctions
    };


    // define a variable
    void define(char *name)
    {
      DEBUG_PRINT("defining a variable");
      prependNew(name, variable, sizeof(dictElt) - sizeof(dictElt::compFunc))->val = 0;
    }

    void define(char *name, void (*funcPtr)(void))
    {
      DEBUG_PRINT("defining a predefined function");
      prependNew(name, predefinedFunction,
                 sizeof(dictElt) - sizeof(dictElt::compFunc))->val = (uint32_t)funcPtr;
    }

    void define(char *name, uint32_t *code, size_t len)
    {
      DEBUG_PRINT("defining a user defined function");
      dictElt *newElt =
        prependNew(name, function,
                   sizeof(dictElt) - sizeof(dictElt::val) - sizeof(dictElt::compFunc));
      //copy code over
      memcpy(&newElt->val, code, len);
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

    dictElt *prependNew(char *name, dictEltType type, size_t eltSize)
    {
      DEBUG_PRINT("prepending new elt")
      dictElt *newElt = (dictElt*)mallocMem(eltSize);
      strncpy(newElt->name, name, MAX_TOKEN_LEN);
      newElt->prev = dictHead;
      newElt->type = type;
      dictHead = newElt;
      DEBUG_PRINT("done prepending");
      return newElt;
    }

};
