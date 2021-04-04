#ifdef _cplusplus
extern "C" {
#endif
  // entry points into SForth
  typedef struct SForth_t SForth_t;

  struct SForth_t
  {
    // to initialize pass a pointer to a callback to print out a string
    void (*begin)(void);

    // call this to have SForth interpret SForth code"
    void (*evaluate)(char *str);

    // call this at the top of loop() to allow SForth to provide an interractive shell
    void (*shellHook)(void);
  };

  extern SForth_t SForth;
#ifdef _cplusplus
}
#endif
