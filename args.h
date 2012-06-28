#ifndef ARGS_H
#define ARGS_H

#define ARGL_COND(lo) (!strcmp(arg, "--" #lo))
#define ARG_COND(sh, lo) (!strcmp(arg, "-" #sh) || ARGL_COND(lo))
#define ARGLN_COND(lo) (ARGL_COND(lo) && argv[*argi+1])
#define ARGN_COND(sh, lo) (ARG_COND(sh, lo) && argv[*argi+1])

/* normal argument */
#define ARG(sh, lo) if (ARG_COND(sh, lo))

/* argument requiring an argn */
#define ARGN(sh, lo) if (ARGN_COND(sh, lo))

/* long-only argument */
#define ARGL(lo) if (ARGL_COND(lo))

/* long-only argument requiring an argn */
#define ARGLN(lo) if (ARGLN_COND(lo))

#endif
