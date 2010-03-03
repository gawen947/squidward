/* File: squidward.c

   Copyright (C) 2008-2009 David Hauweele <david.hauweele@gmail.com>

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
#include <getopt.h>

#include "config.h"

/*
   - Use mmap (and threads?)
   - Color goes in configuration file
   - Show progression
   (- Show top site list (from total))
   (- Show top client (from total))
   (- Decompress on the fly)
*/

#define VERSION      "0.1.5-git"
#define PACKAGE      "squidward"

#ifndef SRS_PATH
#define SRS_PATH     "/usr/local/share/squidward/default.srs"
#endif /* SRS_PATH */

enum max   { STRLEN_MAX = 8192, SLTK_MAX = 5, SRTK_MAX = 2 };
enum sltk  { SLTK_MSEC = 1, SLTK_SRS = 3, SLTK_SZ = 4 };
enum srtk  { SRTK_SRS, SRTK_TYPE };
enum color { CL_RESET, CL_SRS = 31, CL_NAME = 0, CL_VALUE = 1, CL_TITLE = 32 };
enum error { ERR_PARSE, ERR_MEMORY, ERR_LOAD };

#define TYPE(type,pointer) *((type *)pointer)
#define LLU_T(pointer) TYPE(unsigned long long,pointer)
#define DBL_T(pointer) TYPE(double,pointer)

#ifndef ULL_MAX
#define ULL_MAX (unsigned long long)(-1LL)
#endif /* ULL_MAX */

struct type
{
  char *name;
  struct stats *sum;
  struct type *next;
};

struct stats
{
  unsigned long long occ;
  unsigned long long sz;
  unsigned long long msec;
  char *srs;
  const struct type *type;
  struct stats *next;
};

struct ctx
{
  struct type *types;
  struct stats *stats;
  const char **path;
  const char *progname;
  const char *srspath;
  size_t npath;
  size_t size;
  size_t done;
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
  void (*func)(const struct ctx *, const char *, const void *);
};

struct prefix
{
  const char *prefix;
  unsigned long long value;
};

static void error(int error, const char * path, unsigned int line)
{
  switch(error) {
    case ERR_PARSE:
      fprintf(stderr,"In \"%s\" parse error on line %d\n",
              path,line);
      break;
    case ERR_MEMORY:
      fprintf(stderr,"L%d(%s) Out of memory\n",
              line,path);
      break;
    case ERR_LOAD:
      fprintf(stderr,"Loading \"%s\" : ",path);
      perror(NULL);
      break;
    default:
      fprintf(stderr,"Unknown error %x (%s-%d)\n",error,path,line);
      break;
  }
  exit(EXIT_FAILURE);
}

static void *_xmalloc(size_t size, unsigned int line)
{
  register void *mblk = malloc(size);
  if(mblk)
    return mblk;
  error(ERR_MEMORY, __FILE__, line);
}
#define xmalloc(size) _xmalloc(size,__LINE__)

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

static struct stats *add_stats(const char *srs, const struct type *type,
                               struct ctx *ctx)
{
  struct stats *old = ctx->stats;
  struct stats *new = xmalloc(sizeof(struct stats));
  memset(new,0x00,sizeof(struct stats));
  new->next = old;
  new->type = type;
  new->srs  = xmalloc(strlen(srs)+1);
  strcpy(new->srs,srs);
  ctx->stats = new;
  return new;
}

static struct type *add_type(const char *type, struct ctx *ctx)
{
  struct type *old = ctx->types;
  struct type *new = xmalloc(sizeof(struct type));
  struct stats *sum = xmalloc(sizeof(struct stats));
  memset(sum,0x00,sizeof(struct stats));
  new->next = old;
  new->name = xmalloc(strlen(type)+1);
  strcpy(new->name,type);
  ctx->types = new;
  sum->type = new;
  sum->srs  = new->name;
  new->sum = sum;
  return new;
}

#define assert_srtk(token, size) (size == SRTK_MAX)
static void parse_srs(char *buf, unsigned int line, struct ctx *ctx)
{
  char *token[SRTK_MAX];
  size_t tked = tokenize(buf,token," \t\n",SRTK_MAX);
  char *tk_srs,*tk_type;
  if(!assert_srtk(token,tked))
    error(ERR_PARSE,ctx->srspath,line);
  tk_srs  = token[SRTK_SRS];
  tk_type = token[SRTK_TYPE];
  struct type *i;
  for(i = ctx->types ; i ; i = i->next)
    if(!strcmp(tk_type,i->name))
      break;
  if(!i)
    i = add_type(tk_type,ctx);
  add_stats(tk_srs,i,ctx);
}

static void load_ctx(struct ctx *ctx)
{
  FILE *fp = fopen(ctx->srspath,"r");
  if(!fp)
    error(ERR_LOAD,ctx->srspath,0);
  char buf[STRLEN_MAX];
  unsigned int line;
  for(line = 1 ; fgets(buf,STRLEN_MAX,fp) ; line++)
    parse_srs(buf,line,ctx);
  fclose(fp);
}

static void init_ctx(struct ctx *ctx, const char *progname,
                     const char *srspath)
{
  ctx->stats    = NULL;
  ctx->types    = NULL;
  ctx->empty    = false;
  ctx->unk      = false;
  ctx->srspath  = srspath;
  ctx->progname = progname;
  ctx->size     = 0;
  ctx->done     = 0;
  ctx->progress = false;
  ctx->human    = false;
  ctx->stdin    = false;
  ctx->color    = false;
}

static void free_types(struct type *i)
{
  if(!i)
    return;
  free_types(i->next);
  free(i->name);
  free(i->sum);
  free(i);
}

static void free_stats(struct stats *i)
{
  if(!i)
    return;
  free_stats(i->next);
  free(i->srs);
  free(i);
}

static void free_ctx(struct ctx *ctx)
{
  free_stats(ctx->stats);
  free_types(ctx->types);
  free(ctx->path);
}

static void append(struct stats *stat, unsigned long long sz,
                   unsigned long long msec)
{
  stat->occ++;
  stat->sz += sz;
  stat->msec += msec;
}

static void sum(struct stats *stat, unsigned long long occ,
                unsigned long long sz, unsigned long long msec)
{
  stat->occ += occ;
  stat->sz += sz;
  stat->msec += msec;
}

#define assert_sltk(token, size) (size == SLTK_MAX)
static void match(char *buf, unsigned int line, struct ctx *ctx,
                  const char *path)
{
  char *token[SLTK_MAX];
  char *tk_srs;
  size_t tked = tokenize(buf,token," \t",SLTK_MAX);
  unsigned long tk_sz, tk_msec;
  if(!assert_sltk(token,tked))
    error(ERR_PARSE,path,line);
  tk_srs  = strtok(token[SLTK_SRS],"/");
  tk_sz   = atoi(token[SLTK_SZ]);
  tk_msec = atoi(token[SLTK_MSEC]);
  if(!tk_srs)
    error(ERR_PARSE,path,line);
  struct stats *i;
  for(i = ctx->stats ; i ; i = i->next) {
    if(!strcmp(tk_srs,i->srs)) {
      append(i,tk_sz,tk_msec);
      return;
    }
  }
  i = add_stats(tk_srs,NULL,ctx);
  append(i,tk_sz,tk_msec);
}

static void proceed(struct ctx *ctx)
{
  int i,line,fd;
  size_t length;
  ssize_t c;
  char *start;
  register char *buf = xmalloc(R_SIZE);
  char *str = xmalloc(STRLEN_MAX);
  char *ibuf = buf;
  char *istr = str;
  if(ctx->stdin)
    for(line = 1 ; fgets(buf,STRLEN_MAX,stdin) ; line++)
      match(buf,line,ctx,"stdin");
  for(i = 0 ; i < ctx->npath ; i++) {
    fd = open(ctx->path[i],O_RDONLY|O_NDELAY);
    if(!fd)
      error(ERR_LOAD,ctx->path[i],0);

    line = 1;
    while(c = read(fd,buf,R_SIZE)) {
      start = ibuf;
      while(c--) {
        if(*buf == '\n') {
          *buf = '\0';
          /* FIXME: that's quit ugly */
          length = buf - start;
          if(start == ibuf) {
            if(str - istr + length < STRLEN_MAX)
              memcpy(str,start,length);
            else
              error(ERR_PARSE,ctx->path[i],line);
            /* TODO: progress */
            match(istr,line,ctx,ctx->path[i]);
            str = istr;
          }
          else
            match(start,line,ctx,ctx->path[i]);
          line++;
          start = buf + 1;
        }
        buf++;
      }
      length = buf - start;
      if(str - istr + length < STRLEN_MAX) {
        memcpy(str,start,length);
        str += length;
      }
      else
        error(ERR_PARSE,ctx->path[i],line);
      buf = ibuf;
    }
    free(ibuf);
    free(istr);
    close(fd);
  }
}

static void cl(const struct ctx *ctx, enum color color)
{ if(ctx->color) printf("\033[%dm",color); }

static void show_disp(const struct ctx *ctx,
                      const struct show *disp, size_t max)
{
  if(!ctx->human) {
    printf(" ");
    cl(ctx,CL_VALUE);
    disp->func(ctx,disp->unit,disp->value);
    cl(ctx,CL_RESET);
    return;
  }
  size_t i;
  cl(ctx,CL_NAME);
  printf("  %s",disp->name);
  for(i = strlen(disp->name) ; i <= max ; i++)
    printf(" ");
  printf(": ");
  cl(ctx,CL_RESET);
  cl(ctx,CL_VALUE);
  disp->func(ctx,disp->unit,disp->value);
  cl(ctx,CL_RESET);
  printf("\n");
}

static void show_str(const struct ctx *ctx, const char *unit,
                     const void *value)
{ printf("%s",(const char*)value); }

static void show_llu(const struct ctx *ctx, const char *unit,
                     const void *value)
{
  if(!ctx->human) {
    printf("%llu",LLU_T(value));
    return;
  }
  struct prefix pref[] =
    {
      {"", 1LL},
      {"K",1000LL},
      {"M",1000000LL},
      {"G",1000000000LL},
      {"T",1000000000000LL},
      {NULL,ULL_MAX}
    };
  struct prefix *i = pref+1;
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

static void show_time(const struct ctx *ctx, const char *unit,
                      const void *value)
{
  if(!ctx->human) {
    printf("%f",DBL_T(value));
    return;
  }
  struct prefix *i;
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

static void show_double(const struct ctx *ctx, const char *unit,
                        const void *value)
{
  if(!ctx->human) {
    printf("%f",DBL_T(value));
    return;
  }
  struct prefix *i;
  struct prefix pref[] =
    {
      {"",1LL},
      {"K",1000LL},
      {"M",1000000LL},
      {"G",1000000000LL},
      {"T",1000000000000LL},
      {NULL,ULL_MAX},
    };
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

static void show_title(const struct ctx *ctx, const char *title)
{
  if(!ctx->human)
    return;
  cl(ctx,CL_TITLE);
  printf("[-%s-]\n\n",title);
  cl(ctx,CL_RESET);
}

static void show_stats(const struct ctx *ctx, const struct stats *stat)
{
  if((!ctx->unk && !stat->type && stat->next) ||
     (!ctx->empty && !stat->occ))
    return;
  size_t max = 0;
  size_t size;
  double sec = (double)stat->msec/1000;
  double avg_speed = stat->msec ? stat->sz/sec : -1;
  double avg_reqsz = stat->occ ? (double)stat->sz/stat->occ : -1;
  const struct show *i;
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

static void show(struct ctx *ctx)
{
  struct stats *i;
  struct type  *j;
  struct stats unk   = {0,0,0,"UNKNOWN",NULL,NULL};
  struct stats total = {0,0,0,"TOTAL",NULL,NULL};
  show_title(ctx,"RESULTS");
  for(i = ctx->stats ; i ; i = i->next) {
    show_stats(ctx,i);
    sum(i->type ? i->type->sum : &unk,
        i->occ,i->sz,i->msec);
    sum(&total,i->occ,i->sz,i->msec);
  }
  show_title(ctx,"SUM");
  for(j = ctx->types ; j ; j = j->next)
    show_stats(ctx,j->sum);
  show_stats(ctx,&unk);
  show_stats(ctx,&total);
}

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
  const char *opts_help[] = {
    "Print version information.",
    "Print this message.",
    "Show human readable results.",
    "Select the file containing squid request stats.",
    "Use colors to show results.",
    "Show empty stats too.",
    "Show unknown stats too.",
    "Show progression.",
    "Read from stdin.",
  };
  struct stat info;
  struct option *opt;
  const char **hlp;
  int i,c,max,size;
  while(1) {
    c = getopt_long(argc,argv,"VhHS:ceups",opts,NULL);
    if(c == -1)
      break;
    switch(c) {
      case 'V':
        printf(PACKAGE "-" VERSION "\n");
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
      default:
        fprintf(stderr,"Usage: %s [OPTIONS] [FILES]\n", ctx->progname);
        max = 0;
        for(opt = opts ; opt->name; opt++) {
          size = strlen(opt->name);
          if(size > max)
            max = size;
        }
        for(opt = opts, hlp = opts_help ;
            opt->name ;
            opt++,hlp++) {
          fprintf(stderr,"  -%c, --%s",
                  opt->val, opt->name);
          size = strlen(opt->name);
          for(; size < max ; size++)
            fprintf(stderr," ");
          fprintf(stderr," %s\n",*hlp);
        }
        exit(EXIT_FAILURE);
    }
  }
  ctx->npath = argc-optind;
  ctx->path = xmalloc(sizeof(const char *)*ctx->npath);
  for(i = optind ; i < argc ; i++) {
    /* TODO: check for error */
    stat(argv[i],&info);
    ctx->size += info.st_size;
    ctx->path[i-optind] = argv[i];
  }
}

int main(int argc, char *argv[])
{
  struct ctx ctx;
  const char *progname;
  progname = (const char *)strrchr(argv[0],'/');
  progname = progname ? (progname + 1) : argv[0];
  init_ctx(&ctx,progname,SRS_PATH);
  cmdline(argc,argv,&ctx);
  load_ctx(&ctx);
  proceed(&ctx);
  show(&ctx);
  free_ctx(&ctx);
  exit(EXIT_SUCCESS);
}
