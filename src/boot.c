#define __NOLIBBASE__

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/dos.h>

#include <exec/types.h>
#include <exec/libraries.h>
#include <libraries/expansionbase.h>
#if INCLUDE_VERSION > 36
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <libraries/expansion.h>
#else
#include <libraries/dos.h>
#include <libraries/dosextens.h>
#include <libraries/filehandler.h>
#include <libraries/romboot_base.h>
// not defined in 1.3 headers
#ifndef MKBADDR
#define MKBADDR(x) ((BPTR)(((ULONG)x) >> 2))
#endif
#ifndef ADNF_STARTPROC
#define ADNF_STARTPROC	(1<<0)
#endif
#ifndef ERT_ZORROII
#define ERT_ZORROII		ERT_NEWBOARD
#endif
#endif
#include <string.h>

#include "compiler.h"
#include "debug.h"
#include "mydev.h"
#include "disk.h"

static const char execName[] = "romdisk.device";

extern struct DiagArea myDiagArea;

static ULONG *create_param_pkt(struct DevBase *base, ULONG *size)
{
  *size = 21 * 4;
  ULONG *paramPkt = (ULONG *)AllocMem(*size, MEMF_PUBLIC|MEMF_CLEAR);
  if(paramPkt == NULL) {
    return NULL;
  }

  struct DiskHeader *hdr = base->diskHeader;

  paramPkt[0] = (ULONG)hdr->name;
  paramPkt[1] = (ULONG)execName;
  paramPkt[2] = 0; /* unit */
  paramPkt[3] = 0; /* open flags */
  /* env block */
  paramPkt[4] = 16;                 /* size of table */
  paramPkt[5] = 512>>2;             /* 0 # longwords in a block */
  paramPkt[6] = 0;                  /* 1 sector origin -- unused */
  paramPkt[7] = hdr->heads;         /* 2 number of surfaces */
  paramPkt[8] = 1;                  /* 3 secs per logical block -- leave as 1 */
  paramPkt[9] = hdr->sectors;       /* 4 blocks per track */
  paramPkt[10] = 2;                 /* 5 reserved blocks -- 2 boot blocks */
  paramPkt[11] = 0;                 /* 6 ?? -- unused */
  paramPkt[12] = 0;                 /* 7 interleave */
  paramPkt[13] = 0;                 /* 8 lower cylinder */
  paramPkt[14] = hdr->cylinders-1;  /* 9 upper cylinder */
  paramPkt[15] = hdr->num_buffers;  /* a number of buffers */
  paramPkt[16] = MEMF_PUBLIC;       /* b type of memory for buffers */
  paramPkt[17] = 0x7fffffff;        /* c largest transfer size (largest signed #) */
  paramPkt[18] = 0xffffffff;        /* d addmask */
  paramPkt[19] = hdr->boot_prio;    /* e boot priority */
  paramPkt[20] = hdr->dos_type;     /* f dostype: 'DOS\0' */

  return paramPkt;
}

static VOID MyNewList(struct List * list) {
  list->lh_Head          = (struct Node *) &list->lh_Tail;
  list->lh_Tail          = NULL;
  list->lh_TailPred      = (struct Node*) &list->lh_Head;
}

// see ~/Downloads/Amiga/os-source/v40_src/kickstart/expansion/disks.asm
static BOOL enterDosNode(struct DevBase *base, BYTE boot_prio, BYTE flags, struct DeviceNode *deviceNode)
{
  // TODO: implement? this will necer succeed, because OpenDosLib alwayss fails
#if 1
  return 0;
#else
  const BYTE MAXDEVICENAME = 32;
  BOOL ok = FALSE;
  // http://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_2._guide/node00FA.html#line45
  struct DosLibrary *DOSBase = (struct DosLibrary *)(OpenLibrary("dos.library", 0));
  if(DOSBase != NULL) {
    D(("open dos lib ok=%d\n", DOSBase));
    Forbid();
    // see http://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_2._guide/node0078.html
    struct RootNode* rn = (struct RootNode*)(((struct DosLibrary *)DOSBase)->dl_Root);
    struct DosInfo *dosinfo = (struct DosInfo*)BADDR(rn->rn_Info);

    //struct DevInfo *di_head = ((struct DevInfo*)BADDR(dosinfo->di_DevInfo));
    deviceNode->dn_Next = dosinfo->di_DevInfo; // link current head as successor (both BPTR)
    dosinfo->di_DevInfo = MKBADDR(deviceNode); // insert node as new list head (is BPTR)
    Permit();

    if(flags & ADNF_STARTPROC) {
      D(("removing STARTPROC\n"));
      char devNameWithColon[MAXDEVICENAME + 1 + 1]; // ':' will be added
      char* name = (char*)BADDR(deviceNode->dn_Name);
      BYTE namelen = *(name++);
      BYTE namelen2 = strlen(name);
      namelen = (namelen < namelen2) ? namelen : namelen2;
      if(namelen <= MAXDEVICENAME) {
        strncpy(devNameWithColon, name, namelen);
        devNameWithColon[namelen] = ':';
        devNameWithColon[namelen+1] = 0;
        FreeDeviceProc(GetDeviceProc(devNameWithColon, 0));
        ok = TRUE;
        D(("FreeDeviceProc\n"));
      }
      else {
        D(("error: device name length too long=%d\n", namelen));
      }
    }
    else {
      ok = TRUE;
    }

    CloseLibrary((struct Library*)DOSBase);
  }
  else {
    D(("open dos lib failed\n"));
  }

  return ok;
  #endif
}

static BOOL AddBootNodeV34(struct DevBase *base, BYTE boot_prio, BYTE flags, struct DeviceNode *deviceNode, struct ConfigDev *configDev, struct ExpansionBase* ExpansionBase) {
  /*
      move.l	a6,-(sp)		save expansionbase again
      movea.l	hd_SysLib(a5),a6
      moveq.l	#BootNode_SIZEOF,d0
      bsr	GetPubMem		get bootnode memory
      tst.l	d0
      beq.s	10$			didn't get it

      movea.l	d0,a1
      move.b	#NT_BOOTNODE,LN_TYPE(a1)
      move.b	d3,LN_PRI(a1)		set up boot priority
      move.l	hd_ConfigDev(a5),LN_NAME(a1)
      move.l	a4,bn_DeviceNode(a1)
      move.l	(sp),a0			get back expansionbase
      lea.l	eb_MountList(a0),a0
      ; preserves all regs
      jsr	_LVOForbid(a6)		gotta Forbid() around this
      jsr	_LVOEnqueue(a6)		add our bootnode to the list
      jsr	_LVOPermit(a6)		gotta Permit() now
  */

  BOOL ok = FALSE;

  ok = enterDosNode(base, boot_prio, flags, deviceNode);
  if(!ok) {
    D(("enterDosNode() failed, trying to use Enqueue()\n"));
    // http://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_2._guide/node00FA.html#line45
    // http://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_2._guide/node0091.html
    struct BootNode *bn = (struct BootNode *)AllocMem(sizeof(*bn), MEMF_PUBLIC|MEMF_CLEAR);
    if(bn) {
      bn->bn_Node.ln_Type = NT_BOOTNODE; 
      bn->bn_Node.ln_Pri = boot_prio;
      bn->bn_Node.ln_Name = (char *)configDev;
      bn->bn_Flags = 0;
      bn->bn_DeviceNode = (CPTR)deviceNode;

      Forbid();
      Enqueue(&ExpansionBase->MountList, &bn->bn_Node); // http://www.theflatnet.de/pub/cbm/amiga/AmigaDevDocs/exec.html#enqueue()
      Permit();
      ok = TRUE;
      D(("Enqueue() in Mountlist\n"));
    }
  }

  return ok;
}

BOOL boot_init(struct DevBase *base)
{
  BOOL ok = FALSE;
  D(("boot_init, base->sysBase=%08lx\n", base->sysBase)); // used by Amiga OpenLibrary macro

  struct ExpansionBase *ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 33); // Kick 1.2 or newer
  D(("ExpansionBase=%08lx\n", ExpansionBase));
  if(ExpansionBase != NULL) {
    struct ConfigDev *cd = AllocConfigDev();
    D(("got expansion. config dev=%08lx\n", cd));
    if(cd != NULL) {

      /* get diag address */
      ULONG diag_addr = (ULONG)&myDiagArea;
      ULONG diag_base = diag_addr & ~0xffff;
      ULONG diag_off  = diag_addr & 0xffff;
      D(("diag_addr: base=%08lx offset=%04lx\n", diag_base, diag_off));

      /* fill faked config dev */
      cd->cd_Flags = 0;
      cd->cd_Pad = 0;
      cd->cd_BoardAddr = (APTR)diag_base;
      cd->cd_BoardSize = (APTR)0x010000;
      cd->cd_SlotAddr = 0;
      cd->cd_SlotSize = 0;
      cd->cd_Driver = (APTR)base;
      cd->cd_NextCD = NULL;
      cd->cd_Unused[0] = 0;
      cd->cd_Unused[1] = 0;
      cd->cd_Unused[2] = 0;
      cd->cd_Unused[3] = 0;
      struct ExpansionRom *rom = &cd->cd_Rom;
      rom->er_Type = ERT_ZORROII | ERTF_DIAGVALID | 1; /* size=64 KiB */
      rom->er_Flags = ERFF_NOSHUTUP;
      rom->er_Product = 42;
      rom->er_Manufacturer = 2011; /* hack id */
      rom->er_SerialNumber = 1;
      rom->er_InitDiagVec = (UWORD)diag_off;
      rom->er_Reserved03 = 0;

      /* fake copy of diag area. the pointer is stored in er_Reserved0c..0f */
      ULONG *ptr = (ULONG *)&rom->er_Reserved0c;
      *ptr = diag_addr;

      AddConfigDev(cd);

      ULONG paramSize = 0;
      ULONG *paramPkt = create_param_pkt(base, &paramSize);
      D(("got param pkt=%08lx\n", paramPkt));
      if(paramPkt != NULL) {
        struct DeviceNode *dn = MakeDosNode(paramPkt);
        D(("got dos node=%08lx\n", dn));
        if(dn != NULL) {
          /* now add boot node */
          struct DiskHeader *hdr = base->diskHeader;
          BYTE boot_prio = (BYTE)hdr->boot_prio;

          //if(((struct Library*)ExpansionBase)->lib_Version >= 36) {
          //if(((struct Library*)ExpansionBase)->lib_Version >= 38) {
          if(0) {
              ok = AddBootNode( boot_prio, ADNF_STARTPROC, dn, cd );
              D(("add boot node(v36+)=%ld\n", (ULONG)ok));
          }
          else {
              ok = AddBootNodeV34( base, boot_prio, ADNF_STARTPROC, dn, cd, ExpansionBase);
              D(("add boot node=%ld\n", (ULONG)ok));
          }
        }
        FreeMem(paramPkt, paramSize);
      }
    }
    CloseLibrary((struct Library*)ExpansionBase);
  }
  return ok;
}
