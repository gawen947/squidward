/* File: squidward.c

   Copyright (C) 2008-2010 David Hauweele <david.hauweele@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <sysexits.h>
#include <errno.h>
#include <err.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_CONFIG
#include "config.h"
#endif /* HAVE_CONFIG */

#define VERSION      "0.1.6-git"
#define PACKAGE      "squidward"
#ifndef COMMIT
# define PACKAGE_VERSION VERSION
#else
# define PACKAGE_VERSION VERSION " (commit:" COMMIT ")" /* add git commit
                                                           when available */
#endif

#ifndef SRS_PATH
#define SRS_PATH     "/usr/local/share/squidward/default.srs"
#endif /* SRS_PATH */

enum max   { STRLEN_MAX = 8192, SLTK_MAX = 5, SRTK_MAX = 2 };
enum sltk  { SLTK_MSEC = 1, SLTK_SRS = 3, SLTK_SZ = 4 };
enum srtk  { SRTK_SRS, SRTK_TYPE };
enum color { CL_RESET, CL_SRS = 31, CL_NAME = 0, CL_VALUE = 1, CL_TITLE = 32 };

#define PCT_EPS 1.

#define TYPE(type,pointer) *((type *)pointer)
#define LLU_T(pointer) TYPE(unsigned long long,pointer)
#define DBL_T(pointer) TYPE(double,pointer)

#ifndef ULL_MAX
#define ULL_MAX (unsigned long long)(-1LL)
#endif /* ULL_MAX */

/* type of request */
struct type
{
  char *name;        /* defined in srs file */
  struct stats *sum; /* stats for each status */
  struct type *next;
};

/* statistics for a specific status */
struct stats
{
  /* stats */
  unsigned long long occ;  /* nb of req. */
  unsigned long long sz;   /* total size of req. */
  unsigned long long msec; /* total time of req. */

  /* id */
  char *srs;         /* squid request status */
  const struct type *type; /* parent type */
  struct stats *next;
};

/* squidward context */
struct ctx
{
  /* data */
  struct type *types;
  struct stats *stats;

  /* cmdline args */
  const char **path;    /* files to parse */
  const char *progname;
  const char *srspath;  /* path the the srs file */
  size_t npath;         /* nb of files to parse */

  /* progression */
  size_t size;          /* nb of bytes to parse */
  size_t done;          /* nb of bytes already parsed */
  size_t o_pct;         /* old progression */

  /* cmdline flags */
  bool empty;
  bool unk;
  bool human;
  bool progress;
  bool stdin;
  bool color;
};

struct show
{
  const char *name;
  const char *unit;
  const void *value;

  /* display helper */
  void (*func)(const struct ctx *, const char *, const void *);
};

/* prefix (Kilo, Mega, ...) */
struct prefix
{
  const char *prefix;       /* notation */
  unsigned long long value; /* factor */
};

static void *xmalloc(size_t size)
{
  register void *mblk = malloc(size);
  if(mblk)
    return mblk;
  errx(EX_OSERR, "out of memory");
  return NULL; /* avoid a warning from the compiler */
}

/* split a string into an array of token */
static size_t tokenize(char *str, char **token, const char *sep, size_t size)
{
  unsigned int i;
  token[0] = strtok(str,sep);
  if(!token[0])
    return 0;
  for(i = 1 ; i < size ; i++) {
    token[i] = strtok(NULL,sep);
    if(!token[i])
      break;
  }
  return i;
}

/* add statistic to one type */
static struct stats *add_stats(const char *srs, const struct type *type,
                               struct ctx *ctx)
{
  struct stats *old = ctx->stats;
  struct stats *new = xmalloc(sizeof(struct stats));
  memset(new,0x00,sizeof(struct stats));
  new->next = old;
  new->type = type;
  new->srs  = xmalloc(strlen(srs));
  strcpy(new->srs,srs);
  ctx->stats = new;
  return new;
}

/* add type from srs file */
static struct type *add_type(const char *type, struct ctx *ctx)
{
  struct type *old = ctx->types;
  struct type *new = xmalloc(sizeof(struct type));
  struct stats *sum = xmalloc(sizeof(struct stats));
  memset(sum,0x00,sizeof(struct stats));
  new->next = old;
  new->name = xmalloc(strlen(type));
  strcpy(new->name,type);
  ctx->types = new;
  sum->type = new;
  sum->srs  = new->name;
  new->sum = sum;
  return new;
}

/* parse a line from the srs file */
#define assert_srtk(token, size) (size == SRTK_MAX)
static void parse_srs(char *buf, unsigned int line, struct ctx *ctx)
{
  char *token[SRTK_MAX];
  size_t tked = tokenize(buf,token," \t\n",SRTK_MAX);
  char *tk_srs,*tk_type;

  if(!assert_srtk(token,tked)) {
    warnx("parse error at line %d in file ‘%s’: "
          "Expect %d fields got %d",line,ctx->srspath,SRTK_MAX,tked);
    return;
  }

  tk_srs  = token[SRTK_SRS];  /* squid request status */
  tk_type = token[SRTK_TYPE]; /* type name */

  /* add type if it doesn't exit */
  register struct type *i;
  for(i = ctx->types ; i ; i = i->next)
    if(!strcmp(tk_type,i->name))
      break;
  if(!i)
    i = add_type(tk_type,ctx);

  /* add associated stat to type
     specified in that line */
  add_stats(tk_srs,i,ctx);
}

/* load squid request status file */
static void load_ctx(struct ctx *ctx)
{
  FILE *fp = fopen(ctx->srspath,"r");

  if(!fp)
    err(EX_OSFILE,"could not open ‘%s’",ctx->srspath);

  /* parse each line */
  char buf[STRLEN_MAX];
  register unsigned int line;
  for(line = 1 ; fgets(buf,STRLEN_MAX,fp) ; line++)
    parse_srs(buf,line,ctx);
  fclose(fp);
}

/* initialize context with default option */
static void init_ctx(struct ctx *ctx, const char *progname,
                     const char *srspath)
{
  /* everything else is 0, NULL or false */
  memset(ctx,0x00,sizeof(struct ctx));
  ctx->srspath  = srspath;
  ctx->progname = progname;
}

/* free types list */
static void free_types(struct type *list)
{
  register struct type *l;
  for(l = list ; l ; l = l->next) {
    free(l->name);
    free(l->sum);
    free(l);
  }
}

/* free stats list */
static void free_stats(struct stats *list)
{
  register struct stats *l;
  for(l = list ; l ; l = l->next) {
    free(l->srs);
    free(l);
  }
}

/* cleanup context */
static void free_ctx(struct ctx *ctx)
{
  free_stats(ctx->stats);
  free_types(ctx->types);
  free(ctx->path);
}

/* append an occurence */
static void append(struct stats *stat, unsigned long long sz,
                   unsigned long long msec)
{
  stat->occ++;
  stat->sz += sz;
  stat->msec += msec;
}

/* append an occurence to a total type */
static void sum(struct stats *stat, unsigned long long occ,
                unsigned long long sz, unsigned long long msec)
{
  stat->occ += occ;
  stat->sz += sz;
  stat->msec += msec;
}

/* parse a line from log file */
#define assert_sltk(token, size) (size == SLTK_MAX)
static void match(char *buf, unsigned int line, struct ctx *ctx,
                  const char *path)
{
  char *token[SLTK_MAX];
  char *tk_srs;
  size_t tked = tokenize(buf,token," \t",SLTK_MAX);
  unsigned long tk_sz, tk_msec;
  if(!assert_sltk(token,tked)) {
    warnx("parse error at line %d in file ‘%s’: "
          "Expect %d fields got %d",line,path,SLTK_MAX,tked);
    return;
  }

  tk_srs  = strtok(token[SLTK_SRS],"/"); /* squid request status */
  tk_sz   = atoi(token[SLTK_SZ]);        /* request size */
  tk_msec = atoi(token[SLTK_MSEC]);      /* request time */

  if(!tk_srs) {
    warnx("parse error at line %d in file ‘%s’: "
          "Wrong squid request status",line,path);
    return;
  }

  /* append request to the first matching stat */
  struct stats *i;
  for(i = ctx->stats ; i ; i = i->next) {
    if(!strcmp(tk_srs,i->srs)) {
      append(i,tk_sz,tk_msec);
      return;
    }
  }

  /* if it doesn't exist create a new one */
  i = add_stats(tk_srs,NULL,ctx);
  append(i,tk_sz,tk_msec);
}

/* show parsing progression */
static void show_prog(struct ctx *ctx)
{
  size_t space;
  unsigned long long pct;

  if(!ctx->progress)
    return;
  if(!ctx->human) {
    printf("%d/%d\r",ctx->done,ctx->size);
    return;
  }

  pct = (100 * ctx->done) / ctx->size;
  if(pct > ctx->o_pct) {
    ctx->o_pct = pct;
    space = 100 - pct;
    printf("%3lld%% [",pct);
    while(pct--)
      printf("=");
    printf(">");
    while(space--)
      printf(" ");
    printf("]\r");
    fflush(stdout);
  }
}

static void end_prog(struct ctx *ctx)
{
  /* FIXME: clear line */
  if(ctx->progress)
    printf("\n");
}

/* parse each log files specified */
static void proceed(struct ctx *ctx)
{
  register unsigned int line;
  int i;
  FILE *fp;
  char buf[STRLEN_MAX];

  /* use stdin as specified */
  if(ctx->stdin)
    for(line = 1 ; fgets(buf,STRLEN_MAX,stdin) ; line++)
      match(buf,line,ctx,"stdin");

  /* proceed each files for parsing */
  for(i = 0 ; i < ctx->npath ; i++) {
    fp = fopen(ctx->path[i],"r");
    if(!fp)
      err(EX_OSFILE,"could not open ‘%s’",ctx->path[i]);

    /* parse each line */
    for(line = 1 ; fgets(buf,STRLEN_MAX,fp) ; line++) {
      ctx->done += strlen(buf);
      match(buf,line,ctx,ctx->path[i]);
      show_prog(ctx);
    }
    end_prog(ctx);

    fclose(fp);
  }
}

/* display helper for colored output */
static void cl(const struct ctx *ctx, enum color color)
{ if(ctx->color) printf("\033[%dm",color); }

/* display specific results */
static void show_disp(const struct ctx *ctx,
                      const struct show *disp, size_t max)
{
  /* simplified output just show the value */
  if(!ctx->human) {
    printf(" ");
    cl(ctx,CL_VALUE);
    disp->func(ctx,disp->unit,disp->value);
    cl(ctx,CL_RESET);
    return;
  }

  cl(ctx,CL_NAME);
  printf("  %s",disp->name);

  /* padding */
  register size_t i;
  for(i = strlen(disp->name) ; i <= max ; i++)
    printf(" ");

  printf(": ");
  cl(ctx,CL_RESET);
  cl(ctx,CL_VALUE);
  disp->func(ctx,disp->unit,disp->value);
  cl(ctx,CL_RESET);
  printf("\n");
}

/* display helper for string */
static void show_str(const struct ctx *ctx, const char *unit,
                     const void *value)
{ printf("%s",(const char*)value); }

/* display helper for unsigned long long */
static void show_llu(const struct ctx *ctx, const char *unit,
                     const void *value)
{
  /* simplified output just show the value as is */
  if(!ctx->human) {
    printf("%llu",LLU_T(value));
    return;
  }

  /* show prefix */
  struct prefix pref[] =
    {
      {"", 1LL},
      {"K",1000LL},
      {"M",1000000LL},
      {"G",1000000000LL},
      {"T",1000000000000LL},
      {NULL,ULL_MAX}
    };
  register struct prefix *i = pref+1;
  if(LLU_T(value) < i->value) {
    printf("%llu %s",LLU_T(value),unit);
    return;
  }
  for(; i->prefix ; i++) {
    if(LLU_T(value) < (i+1)->value) {
      printf("%2.1f %s%s",
             (double)LLU_T(value)/i->value,
             i->prefix,
             unit);
      return;
    }
  }
}

/* display helper for time */
static void show_time(const struct ctx *ctx, const char *unit,
                      const void *value)
{
  /* simplified output just show the value as is */
  if(!ctx->human) {
    printf("%f",DBL_T(value));
    return;
  }

  /* show time prefix */
  struct prefix pref[] =
    {
      {"seconds",1LL},
      {"minutes",60LL},
      {"hours",3600LL},
      {"days",86400LL},
      {"weeks",604800LL},
      {"months",2592000LL},
      {"years",31536000LL},
      {NULL,ULL_MAX},
    };
  register struct prefix *i;
  for(i = pref ; i->prefix ; i++) {
    if(DBL_T(value) < (double)(i+1)->value) {
      printf("%2.1f %s%s",
             DBL_T(value)/i->value,
             i->prefix,
             unit);
      return;
    }
  }
}

/* display helper for double */
static void show_double(const struct ctx *ctx, const char *unit,
                        const void *value)
{
  /* simplified output show value as is */
  if(!ctx->human) {
    printf("%f",DBL_T(value));
    return;
  }

  /* show prefix */
  struct prefix pref[] =
    {
      {"",1LL},
      {"K",1000LL},
      {"M",1000000LL},
      {"G",1000000000LL},
      {"T",1000000000000LL},
      {NULL,ULL_MAX},
    };
  register struct prefix *i;
  for(i = pref ; i->prefix ; i++) {
    if(DBL_T(value) < (double)(i+1)->value) {
      printf("%2.1f %s%s",
             DBL_T(value)/i->value,
             i->prefix,
             unit);
      return;
    }
  }
}

/* display helper for title */
static void show_title(const struct ctx *ctx, const char *title)
{
  if(!ctx->human)
    return;
  cl(ctx,CL_TITLE);
  printf("[-%s-]\n\n",title);
  cl(ctx,CL_RESET);
}

/* display statistics */
static void show_stats(const struct ctx *ctx, const struct stats *stat)
{
  /* depending on selected options
     some statistics should not be
     displayed */
  if((!ctx->unk && !stat->type && stat->next) ||
     (!ctx->empty && !stat->occ))
    return;

  size_t max = 0;
  size_t size;
  double sec = (double)stat->msec/1000;
  double avg_speed = stat->msec ? stat->sz/sec : -1;
  double avg_reqsz = stat->occ ? (double)stat->sz/stat->occ : -1;

  const struct show disp[] =
    {
      {"Type","",stat->type ? stat->type->name : "UNKNOWN",show_str},
      {"Occurrence","req",&stat->occ,show_llu},
      {"Size","B",&stat->sz,show_llu},
      {"Time","",&sec,show_time},
      {"Average speed","Bps",&avg_speed,show_double},
      {"Average request size","Bpr",&avg_reqsz,show_double},
      {NULL,NULL,NULL,NULL}
    };
  register const struct show *i;
  cl(ctx,CL_SRS);
  printf(ctx->human ? " %s\n" : "%s",stat->srs);
  cl(ctx,CL_RESET);
  for(i = disp ; i->name ; i++) {
    size = strlen(i->name);
    if(size > max)
      max = size;
  }
  for(i = disp ; i->name ; i++)
    show_disp(ctx,i,max);
  printf("\n");
}

/* display results */
static void show(struct ctx *ctx)
{
  /* define default stats for
     unknown and total type */
  struct stats unk   = {0,0,0,"UNKNOWN",NULL,NULL};
  struct stats total = {0,0,0,"TOTAL",NULL,NULL};

  /* display user stats */
  register struct stats *i;
  show_title(ctx,"RESULTS");
  for(i = ctx->stats ; i ; i = i->next) {
    show_stats(ctx,i);
    sum(i->type ? i->type->sum : &unk,
        i->occ,i->sz,i->msec);
    sum(&total,i->occ,i->sz,i->msec);
  }

  /* display default stats */
  register struct type *j;
  show_title(ctx,"SUM");
  for(j = ctx->types ; j ; j = j->next)
    show_stats(ctx,j->sum);
  show_stats(ctx,&unk);
  show_stats(ctx,&total);
}

/* parse command line */
static void cmdline(int argc, char *argv[], struct ctx *ctx)
{
  struct option opts[] =
  {
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 'h'},
    {"human", no_argument, 0, 'H'},
    {"srs", required_argument, 0, 'S'},
    {"color", no_argument, 0, 'c'},
    {"empty", no_argument, 0, 'e'},
    {"unknown", no_argument, 0, 'u'},
    {"progress", no_argument, 0, 'p'},
    {"stdin", no_argument, 0, 's'},
    {NULL,0,0,0}
  };

  /* help message */
  const char *opts_help[] = {
    "Print version information.",
    "Print this message.",
    "Show human readable results.",
    "Select the file containing squid request status.",
    "Use colors to show results.",
    "Show empty stats too.",
    "Show unknown stats too.",
    "Show progression.",
    "Read from stdin.",
  };
  struct stat info;
  struct option *opt;
  const char **hlp;
  int i,c,max,size,ret = EXIT_FAILURE;
  while(1) {
    c = getopt_long(argc,argv,"VhHS:ceups",opts,NULL);
    if(c == -1)
      break;
    switch(c) {
      case 'V':
        printf(PACKAGE "-" PACKAGE_VERSION "\n");
        exit(EXIT_SUCCESS);
      case 'H':
        ctx->human = true;
        break;
      case 'S':
        ctx->srspath = optarg;
        break;
      case 'c':
        ctx->color = true;
        break;
      case 'e':
        ctx->empty = true;
        break;
      case 'u':
        ctx->unk = true;
        break;
      case 'p':
        ctx->progress = true;
        break;
      case 's':
        ctx->stdin = true;
        break;
      case 'h':
        ret = EXIT_SUCCESS;
      default:
        /* display usage */
        fprintf(stderr,"Usage: %s [OPTIONS] [FILES]\n", ctx->progname);
        max = 0;
        for(opt = opts ; opt->name ; opt++) {
          size = strlen(opt->name);
          if(size > max)
            max = size;
        }
        for(opt = opts, hlp = opts_help ; opt->name ; opt++, hlp++) {
          fprintf(stderr,"  -%c, --%s", opt->val, opt->name);
          size = strlen(opt->name);
          for(; size < max ; size++)
            fputc(' ', stderr);
          fprintf(stderr, " %s\n",*hlp);
        }
        exit(ret);
    }
  }

  /* consider remaining arguments
     as file to parse */
  ctx->npath = argc-optind;
  ctx->path = xmalloc(sizeof(const char *)*ctx->npath);
  for(i = optind ; i < argc ; i++) {
    if(stat(argv[i],&info) < 0)
      err(EX_OSFILE, "cannot stat ‘%s’", argv[i]);
    ctx->size += info.st_size;
    ctx->path[i-optind] = argv[i];
  }
}

int main(int argc, char *argv[])
{
  struct ctx ctx; /* create context */

  /* retrieve program's name */
  const char *progname;
  progname = (const char *)strrchr(argv[0],'/');
  progname = progname ? (progname + 1) : argv[0];

  /* initialization stuff */
  init_ctx(&ctx,progname,SRS_PATH);
  cmdline(argc,argv,&ctx);
  load_ctx(&ctx);

  /* proceed and show results */
  proceed(&ctx);
  show(&ctx);

  /* clean up */
  free_ctx(&ctx);
  exit(EXIT_SUCCESS);
}
