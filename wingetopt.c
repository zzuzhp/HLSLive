//#ifndef __GNUC__

#include "wingetopt.h"
#include <stdio.h>

#define NULL	  0
#define EOF	      (-1)
#define ERR(s, c) \
    if (opterr) \
    { \
        char errbuf[2]; \
        errbuf[0] = c; \
        errbuf[1] = '\n'; \
        fputs(argv[0], stderr); \
        fputs(s, stderr); \
        fputc(c, stderr); \
	}

int	   opterr = 1;
int	   optind = 1;
int	   optopt;
char * optarg;

int
getopt(int argc, char ** argv, char * opts)
{
	static   int    sp = 1;
	register int    c;
	register char * cp;

	if (sp == 1)
    {
        if (optind >= argc ||
		    argv[optind][0] != '-' || argv[optind][1] == '\0')
        {
            return EOF;
        }
		else if (strcmp(argv[optind], "--") == NULL)
        {
			++optind;
			return EOF;
		}
    }

	optopt = c = argv[optind][sp];

	if (c == ':' || (cp = strchr(opts, c)) == NULL)
    {
		ERR(": illegal option -- ", c);

		if (argv[optind][++sp] == '\0')
		{
			optind++;
			sp = 1;
		}

		return '?';
	}

	if (*++cp == ':')
    {
		if (argv[optind][sp + 1] != '\0')
		{
            optarg = &argv[optind++][sp + 1];
		}
		else if (++optind >= argc)
        {
			ERR(": option requires an argument -- ", c);

			sp = 1;
			return '?';
		}
		else
		{
            optarg = argv[optind++];
		}

		sp = 1;
	}
	else
    {
		if (argv[optind][++sp] == '\0')
		{
			sp = 1;
			++optind;
		}

		optarg = NULL;
	}

	return c;
}

/***
 *
 *  #include <ctype.h>
 *  #include <stdio.h>
 *  #include <stdlib.h>
 *  #include <unistd.h>
 *
 *  int
 *  main (int argc, char **argv)
 *  {
 *    int aflag = 0;
 *    int bflag = 0;
 *    char *cvalue = NULL;
 *    int index;
 *    int c;
 *
 *    opterr = 0;
 *
 *
 *    while ((c = getopt (argc, argv, "abc:")) != -1)
 *      switch (c)
 *        {
 *        case 'a':
 *          aflag = 1;
 *          break;
 *        case 'b':
 *          bflag = 1;
 *          break;
 *        case 'c':
 *          cvalue = optarg;
 *          break;
 *        case '?':
 *          if (optopt == 'c')
 *            fprintf (stderr, "Option -%c requires an argument.\n", optopt);
 *          else if (isprint (optopt))
 *            fprintf (stderr, "Unknown option `-%c'.\n", optopt);
 *          else
 *            fprintf (stderr,
 *                     "Unknown option character `\\x%x'.\n",
 *                     optopt);
 *          return 1;
 *        default:
 *          abort ();
 *        }
 *
 *
 *    printf ("aflag = %d, bflag = %d, cvalue = %s\n",
 *            aflag, bflag, cvalue);
 *
 *    for (index = optind; index < argc; index++)
 *      printf ("Non-option argument %s\n", argv[index]);
 *    return 0;
 *  }
 *
 *
 *  Here are some examples showing what this program prints with different combinations of arguments:
 *
 *  % testopt
 *  aflag = 0, bflag = 0, cvalue = (null)
 *
 *  % testopt -a -b
 *  aflag = 1, bflag = 1, cvalue = (null)
 *
 *  % testopt -ab
 *  aflag = 1, bflag = 1, cvalue = (null)
 *
 *  % testopt -c foo
 *  aflag = 0, bflag = 0, cvalue = foo
 *
 *  % testopt -cfoo
 *  aflag = 0, bflag = 0, cvalue = foo
 *
 *  % testopt arg1
 *  aflag = 0, bflag = 0, cvalue = (null)
 *  Non-option argument arg1
 *
 *  % testopt -a arg1
 *  aflag = 1, bflag = 0, cvalue = (null)
 *  Non-option argument arg1
 *
 *  % testopt -c foo arg1
 *  aflag = 0, bflag = 0, cvalue = foo
 *  Non-option argument arg1
 *
 *  % testopt -a -- -b
 *  aflag = 1, bflag = 0, cvalue = (null)
 *  Non-option argument -b
 *
 *  % testopt -a -
 *  aflag = 1, bflag = 0, cvalue = (null)
 *  Non-option argument -
 *
 ***/

//#endif  /* __GNUC__ */
