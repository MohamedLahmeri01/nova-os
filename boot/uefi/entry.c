/* NOVA P3.4: experimental independent ELF64 boot from UEFI FAT disk.
 * No physical hardware support; relies on firmware page mappings.
 */
#include <stdint.h>
#include <stddef.h>
#define ABI __attribute__((ms_abi))
#define ERR(n) (UINT64_C(0x8000000000000000)|(n))
#define MAX_ELF (1024u*1024u)
typedef uint64_t status_t;
typedef void *handle_t;
struct guid { uint32_t a; uint16_t b,c; uint8_t d[8]; };
struct header { uint64_t sig; uint32_t rev,bytes,crc,res; };
struct system {
 struct header h; void *vendor; uint32_t revision,pad;
 handle_t in_handle; void *in;
 handle_t out_handle; void *out;
 handle_t err_handle; void *err;
 void *runtime; void *boot; uint64_t count; void *table;
};
struct boot {
 struct header h; void *raise,*restore;
 status_t (ABI *pages)(uint32_t,uint32_t,uint64_t,uint64_t*);
 void *free_pages;
 status_t (ABI *map)(uint64_t*,void*,uint64_t*,uint64_t*,uint32_t*);
 void *alloc_pool,*free_pool;
 void *slots80to144[9];
 status_t (ABI *protocol)(handle_t,const struct guid*,void**);
 void *slots160to224[9];
 status_t (ABI *exit)(handle_t,uint64_t);
};
struct loaded {uint32_t revision,pad; void *parent,*system,*device;};
struct file {
 uint64_t revision;
 status_t (ABI *open)(struct file*,struct file**,const uint16_t*,uint64_t,uint64_t);
 status_t (ABI *close)(struct file*);
 void *delete_;
 status_t (ABI *read)(struct file*,uint64_t*,void*);
};
struct fs {uint64_t revision;status_t (ABI *volume)(struct fs*,struct file**);};
static const struct guid loaded_guid={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
static const struct guid fs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static const uint16_t name[]={ '\\','E','F','I','\\','B','O','O','T','\\','N','O','V','A','K','E','R','N','.','E','L','F',0};
_Static_assert(offsetof(struct system,boot)==96,"system layout");
_Static_assert(offsetof(struct boot,map)==56,"map layout");
_Static_assert(offsetof(struct boot,protocol)==152,"handle protocol layout");
_Static_assert(offsetof(struct boot,exit)==232,"exit layout");
_Static_assert(offsetof(struct file,read)==32,"read layout");
static inline void out(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t in(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static void serial_init(void){out(0x3f9,0);out(0x3fb,0x80);out(0x3f8,1);out(0x3f9,0);out(0x3fb,3);out(0x3fa,0xc7);out(0x3fc,0x0b);}
static void say(const char*s){for(;*s;++s){unsigned n=100000;while(!(in(0x3fd)&32)&&--n){}if(!n)return;out(0x3f8,(uint8_t)*s);}}
static uint8_t elf[MAX_ELF] __attribute__((aligned(16)));
static uint8_t mmap[32768] __attribute__((aligned(16)));
static uint8_t stack[32768] __attribute__((aligned(16)));
static uint16_t rd16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t rd32(const uint8_t*p){return (uint32_t)rd16(p)|((uint32_t)rd16(p+2)<<16);}
static uint64_t rd64(const uint8_t*p){return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);}
static void copy(uint8_t*d,const uint8_t*s,uint64_t n){for(uint64_t i=0;i<n;++i)d[i]=s[i];}
static void zero(uint8_t*d,uint64_t n){for(uint64_t i=0;i<n;++i)d[i]=0;}
static int read_kernel(handle_t image,struct boot*b,uint64_t*len){
 struct loaded*l=0;struct fs*filesystem=0;struct file*root=0,*fd=0;
 if(!b->protocol || b->protocol(image,&loaded_guid,(void**)&l) || !l || !l->device) return 0;
 if(b->protocol(l->device,&fs_guid,(void**)&filesystem) || !filesystem) return 0;
 if(filesystem->volume(filesystem,&root) || !root) return 0;
 int ok=0;
 if(!root->open(root,&fd,name,1,0) && fd){
  uint64_t size=MAX_ELF;
  if(!fd->read(fd,&size,elf) && size>0 && size<MAX_ELF){*len=size;ok=1;}
  fd->close(fd);
 }
 root->close(root);
 return ok;
}
static int load_kernel(struct boot*b,uint64_t size,uint64_t*entry){
 if(size<64 || elf[0]!=127 || elf[1]!='E' || elf[2]!='L' || elf[3]!='F' ||
    elf[4]!=2 || elf[5]!=1 || elf[6]!=1 || rd16(elf+16)!=3 ||
    rd16(elf+18)!=62 || rd16(elf+52)!=64 || rd16(elf+54)!=56)return 0;
 uint64_t start=rd64(elf+24),off=rd64(elf+32),max=0;
 uint16_t count=rd16(elf+56);
 if(!count || count>16 || off>size || (uint64_t)count*56>size-off)return 0;
 int executable=0,loads=0;
 for(uint16_t i=0;i<count;++i){
  const uint8_t*h=elf+off+i*56;
  uint32_t typ=rd32(h), flags=rd32(h+4);
  uint64_t fileoff=rd64(h+8),v=rd64(h+16),fsz=rd64(h+32),msz=rd64(h+40);
  if(typ==2){
   if(fsz%16 || fileoff>size || fsz>size-fileoff)return 0;
   for(uint64_t j=0;j<fsz;j+=16){
    uint64_t tag=rd64(elf+fileoff+j);
    if(tag==7 || tag==8 || tag==9 || tag==17 || tag==18 || tag==19 ||
       tag==23 || tag==35 || tag==36 || tag==37)return 0;
   }
  }
  if(typ!=1)continue;
  if(++loads>8 || !msz || fsz>msz || fileoff>size || fsz>size-fileoff ||
     v>=MAX_ELF || msz>MAX_ELF-v || ((fileoff^v)&4095) ||
     ((flags&3)==3))return 0;
  if(v+msz>max)max=v+msz;
  if((flags&1) && start>=v && start<v+msz)executable=1;
  for(uint16_t j=0;j<i;++j){
   const uint8_t*other=elf+off+j*56;
   if(rd32(other)==1){
    uint64_t a=rd64(other+16),z=rd64(other+40);
    if(v<a+z && a<v+msz)return 0;
   }
  }
 }
 if(!loads || !executable || !max)return 0;
 uint64_t base=0,pages=(max+4095)/4096;
 if(!b->pages || b->pages(0,1,pages,&base) || !base)return 0;
 zero((uint8_t*)(uintptr_t)base,pages*4096);
 for(uint16_t i=0;i<count;++i){
  const uint8_t*h=elf+off+i*56;
  if(rd32(h)==1)copy((uint8_t*)(uintptr_t)(base+rd64(h+16)),elf+rd64(h+8),rd64(h+32));
 }
 *entry=base+start;
 return 1;
}
/* Early ELF entry uses SysV x86-64 ABI; no parameters. Stack aligned for call. */
static void ABI jump_kernel(uint64_t address,uint64_t bytes,uint64_t stride) __attribute__((noreturn));
static void ABI jump_kernel(uint64_t address,uint64_t bytes,uint64_t stride){
 uintptr_t top=(uintptr_t)(stack+sizeof(stack));
 /* SysV ELF kernel: RDI=map pointer, RSI=bytes, RDX=descriptor stride.
  * Firmware services have ended; map is persistent loader-owned storage.
  * Place target in R11 to avoid accidental register conflicts.
  */
 __asm__ volatile("movq %0,%%rsp\n\tandq $-16,%%rsp\n\t"
                  "movq %1,%%r11\n\tmovq %2,%%rdi\n\t"
                  "movq %3,%%rsi\n\tmovq %4,%%rdx\n\t"
                  "call *%%r11\n\tud2"
                  ::"r"(top),"r"(address),"r"((uintptr_t)mmap),"r"(bytes),"r"(stride)
                  :"r11","rdi","rsi","rdx","memory");
 __builtin_unreachable();
}
status_t ABI efi_main(handle_t image,struct system*system){
 serial_init();
 if(!system || system->h.sig!=UINT64_C(0x5453595320494249) || !system->boot){
  say("NOVA-UEFI-FAIL-SYSTEM-TABLE\r\n");return ERR(1);
 }
 struct boot*b=(struct boot*)system->boot;
 say("NOVA-UEFI-X64-ENTRY-OK\r\n");
 uint64_t size=0,entry=0;
 if(!read_kernel(image,b,&size)){say("NOVA-UEFI-ELF-FILE-FAIL\r\n");return ERR(1);}
 say("NOVA-UEFI-ELF-FILE-OK\r\n");
 if(!load_kernel(b,size,&entry)){say("NOVA-UEFI-ELF-LOAD-FAIL\r\n");return ERR(1);}
 say("NOVA-UEFI-ELF-LOAD-OK\r\n");
 if(!b->map || !b->exit)return ERR(1);
 for(unsigned attempt=0;attempt<4;++attempt){
  uint64_t length=sizeof(mmap),key=0,stride=0;uint32_t version=0;
  status_t s=b->map(&length,mmap,&key,&stride,&version);
  if(s || !key || !length || stride<40 || length%stride || version!=1){
   say("NOVA-UEFI-MAP-FAIL\r\n");return ERR(1);
  }
  s=b->exit(image,key);
  if(!s)jump_kernel(entry,length,stride);
  if(s!=ERR(2)){say("NOVA-UEFI-EXIT-FAIL\r\n");return s;}
 }
 say("NOVA-UEFI-EXIT-RETRIES-EXHAUSTED\r\n");return ERR(1);
}
