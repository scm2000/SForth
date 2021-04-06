// Support for compile to machine code on the fly
// presently only supports Cortex M processors.

class CompilationBuffer
{
  public:

    uint16_t *compiledCode;

    CompilationBuffer()
      : blockFactorHalfWords(256)
    {
      compiledCode = 0;
      nextCodePtr = 0;
      halfWordsAllocated = 0;
    }

    void freeUp()
    {
      free(compiledCode);
      compiledCode = 0;
      nextCodePtr = 0;
      halfWordsAllocated = 0;
    }
    
    size_t halfWordCount()
    {
      return nextCodePtr - compiledCode;
    }

    void beginFunction()
    {
      free(compiledCode);
      halfWordsAllocated = blockFactorHalfWords;
      compiledCode = (uint16_t*)malloc(halfWordsAllocated * 2);
      if (!compiledCode)
      {
        myThrow(cantMalloc, "can't allocate temp space for compilation");
      }

      nextCodePtr = compiledCode;

      // function prolog is to push {r3, lr}
      *nextCodePtr++ = 0xb508;
    }

    void endFunction()
    {
      checkAndExtendStorage(2);
      *nextCodePtr++ = 0xbd08;  // this is pop {r3, pc}
      *nextCodePtr++ = 0xbf00;  // this is nop (but why?)
    }

    void insertCallToVoidWithArg(uint32_t callLoc, uint32_t val)
    {
      checkAndExtendStorage(10);
      
      *nextCodePtr++ = 0x4802; // load r0 with number 8 bytes away
      *nextCodePtr++ = 0x4b02; // load r3 with address 8 bytes away
      *nextCodePtr++ = 0x4798; // branch link through r3
      *nextCodePtr++ = 0xbf00; // nop (because data has to be a multiple of 4 away)
      *nextCodePtr++ = 0xe002; // branch over next 8 byte
      *nextCodePtr++ = (val) & 0xffff; // low half word of value
      *nextCodePtr++ = (val) >> 16;    // high half word of value
      *nextCodePtr++ = callLoc & 0xffff; // low half word of func addr
      *nextCodePtr++ = callLoc >> 16;    // high half word of func addr
      *nextCodePtr++ = 0xbf00; // nop for allignment of next instr.
    }

    void insertCallToVoid(uint32_t callLoc)
    {
      checkAndExtendStorage(6);
      *nextCodePtr++ = 0x4b01; // load r3 with address 4 bytes away
      *nextCodePtr++ = 0x4798; // branch link through r3
      *nextCodePtr++ = 0xe001; // branch over next 4 bytes
      *nextCodePtr++ = (callLoc) & 0xffff; // low half word of func addr
      *nextCodePtr++ = (callLoc) >> 16; // high half word of func addr
      *nextCodePtr++ = 0xbf00; // nop for allignment of next instr.
    }
    
  private:

    const size_t blockFactorHalfWords;
    uint16_t *nextCodePtr;
    size_t halfWordsAllocated;

    void checkAndExtendStorage(size_t halfWordsToAdd)
    {
      size_t saveCnt = halfWordCount();

      if (saveCnt + halfWordsToAdd > halfWordsAllocated)
      {
        halfWordsAllocated += blockFactorHalfWords;
        compiledCode = (uint16_t *)reallocf(compiledCode, halfWordsAllocated);
        if (!compiledCode)
        {
          myThrow(cantMalloc, "unable to extend compilation buffer");
        }
        nextCodePtr = compiledCode + saveCnt;
      }
    }
};
