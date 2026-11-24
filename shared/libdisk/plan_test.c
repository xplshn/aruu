/* see LICENSE file for copyright and license details */
/* a plan made on an image, checked, written, saved and read again:
 *   cc -D_GNU_SOURCE -Ishared shared/libdisk/plan_test.c shared/libdisk/plan.c shared/libdisk/label.c \
 *      shared/libdisk/gpt.c shared/libutil/diskutil.c shared/libutil/hexval.c shared/libutil/eprintf.c \
 *      shared/libutil/ealloc.c shared/libutil/strlcpy.c -o plan_test && ./plan_test */
#include "plan.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int nlines; static void line(const char *l, void *a){ (void)a; nlines++; puts(l); }
int main(void){
  char path[]="/tmp/plan_test.XXXXXX"; int fd=mkstemp(path); assert(fd>=0 && ftruncate(fd,(off_t)256<<20)==0); close(fd);
  struct plan before, p; char why[200]; char *argv[16]; char store[256];
  assert(plan_read_disk(&before,path)==0 && before.label.kind==LABEL_NONE);
  p=before;
  { struct BlockDev d; assert(blockdev_open(&d,path,0)==0); assert(label_init(&p.label,&d,LABEL_GPT)==0); blockdev_close(&d); }
  assert(plan_check(&p,PLAN_NEED_ROOT,why,sizeof why)<0);
  assert(plan_add(&p,0,2048,"ef02","BIOS","","","")==0);
  assert(plan_add(&p,0,64*2048,"ef00","ESP","vfat","/boot/efi","ESP")==1);
  assert(plan_add(&p,0,0,"8300","ROOT","ext4","/","")==2);
  assert(plan_check(&p,PLAN_NEED_ROOT|PLAN_NEED_BOOT,why,sizeof why)==0);
  assert(plan_changes(&before,&p,line,NULL)>=3);
  assert(plan_mkfs_argv(&p,2,argv,16,store,sizeof store)==0 && !strcmp(argv[0],"mkfs.ext4") && !strcmp(argv[1],"-F"));
  assert(argv[2] && argv[3] == NULL);
  assert(plan_write_table(&p)==0);
  FILE *f=tmpfile(); assert(plan_save(&p,f)==0); rewind(f);
  struct plan q; assert(plan_load(&q,f,why,sizeof why)==0);
  for(int i=0;i<3;i++){ assert(q.label.part[i].start==p.label.part[i].start && q.label.part[i].end==p.label.part[i].end); assert(!strcmp(q.attr[i].fs,p.attr[i].fs)&&!strcmp(q.attr[i].mount,p.attr[i].mount)); }
  struct plan r; assert(plan_read_disk(&r,path)==0 && r.label.kind==LABEL_GPT && r.label.part[2].used);
  p.attr[1].mount[0]='x'; assert(plan_check(&p,0,why,sizeof why)<0); puts(why);
  plan_set(&p,1,"vfat","/",NULL,1); assert(plan_check(&p,0,why,sizeof why)<0); puts(why);
  unlink(path); puts("plan_test: ok"); return 0; }
