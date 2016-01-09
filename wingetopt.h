/*
#ifdef __GNUC__
#include <getopt.h>
#endif
#ifndef __GNUC__
*/
#ifndef ___WINGETOPT_H___
#define ___WINGETOPT_H___

#ifdef __cplusplus
extern "C" {
#endif

extern int    opterr;
extern int    optind;
extern int    optopt;
extern char * optarg;

extern int getopt(int argc, char ** argv, char * opts);

#ifdef __cplusplus
}
#endif

//#endif  /* ___WINGETOPT_H___ */
#endif  /* __GNUC__ */
