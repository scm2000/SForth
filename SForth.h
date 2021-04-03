#ifdef _cplusplus
extern "C" {
#endif
  // entry points into SForth
  typedef struct SForth_t SForth_t;

  struct SForth_t
  {
    // to initialize pass a pointer to a callback to print out a string
    void (*begin)(void (*outStringFunction)(char*));

    void (*evaluate)(char *str);
  };

  extern SForth_t SForth;
#ifdef _cplusplus
}
#endif
