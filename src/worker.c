#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>

#include <exec/types.h>
#include <exec/ports.h>

#if INCLUDE_VERSION > 36
#include <dos/dostags.h>
#include <dos/dosextens.h>
#else
#include <libraries/dosextens.h>
#include <libraries/dos.h>
#endif
#include <string.h>

#define NO_SYSBASE
#include "compiler.h"
#include "mydev.h"
#include "debug.h"
#include "worker.h"

#define CMD_TERM     0x7ff0

struct InitData
{
  ULONG           initSigMask;
  struct Task    *initTask;
  struct DevBase *base;
};

static const char WorkerTaskName[] = MYDEV_WORKER ".task";

static struct InitData * worker_startup(void)
{
  /* retrieve global sys base */
  struct Library *SysBase = *((struct Library **)4);

  struct Task *task = FindTask(NULL);

  return (struct InitData *)task->tc_UserData;
}

#define SysBase base->sysBase


#define ME_TASK 	0
#define ME_STACK	1
#define NUMENTRIES	2
struct FakeMemEntry {
    ULONG fme_Reqs;
    ULONG fme_Length;
};
struct FakeMemList {
    struct Node fml_Node;
    UWORD	fml_NumEntries;
    struct FakeMemEntry fml_ME[NUMENTRIES];
};
static const struct FakeMemList TaskMemTemplate = {
    { 0 },						/* Node */
    NUMENTRIES, 					/* num entries */
    {							/* actual entries: */
        { MEMF_PUBLIC | MEMF_CLEAR, sizeof( struct Task ) },    /* task */
        { MEMF_PUBLIC | MEMF_CLEAR,	4096 }					/* stack */
    }
};
static const struct FakeMemList TaskMemTemplate2 = {
    { 0 },						/* Node */
    1, 	   				/* num entries */
    {							/* actual entries: */
        { MEMF_PUBLIC | MEMF_CLEAR, sizeof( struct Task ) + 4096 },    /* task */
    }
};

static struct Task * MyCreateTask(struct DevBase *base, void* taskUserData, const char *name, BYTE pri, const APTR initPC, ULONG stackSize)
{
    struct Task *newTask;

    /* round the stack up to longwords... */
    stackSize = (stackSize +3) & 0xFFFFFFFC;

#if 1
    /*
     * This will allocate two chunks of memory: task of PUBLIC
     * and stack of PRIVATE
     */
    struct FakeMemList fakememlist;
    struct MemList *ml;
    fakememlist = TaskMemTemplate2;
    fakememlist.fml_ME[ME_TASK].fme_Length = ((sizeof(struct Task) + 3) & 0xFFFFFFFC) + ((stackSize + 3) & 0xFFFFFFFC) + ((strlen(name) + 1 + 3) & 0xFFFFFFFC);
    //ml = (struct MemList *) AllocEntry( (struct MemList *)&fakememlist );
    ml = (struct MemList *) AllocMem(((sizeof(struct FakeMemList) + 3) & 0xFFFFFFFC) + fakememlist.fml_ME[ME_TASK].fme_Length,
      MEMF_PUBLIC | MEMF_CLEAR);
    fakememlist.fml_NumEntries = 0;

    /* NOTE ! - AllocEntry returns with bit 31 set if it fails ! */
    if( ((ULONG)ml) & 0x80000000 ) {
      D(("MyCreateTask: AllocEntry failed!\n"));
    	return( NULL );
    }

    /* set the stack accounting stuff */
    //newTask = (struct Task *) ml->ml_ME[ME_TASK].me_Addr;
    //newTask->tc_SPLower = (BYTE*)ml->ml_ME[ME_TASK].me_Addr + ((sizeof(struct Task) + 3) & 0xFFFFFFFC);
    ml->ml_ME[ME_TASK].me_Addr = (APTR)((BYTE*)ml + ((sizeof(struct FakeMemList) + 3) & 0xFFFFFFFC));
    newTask = (struct Task *) ml->ml_ME[ME_TASK].me_Addr;
    newTask->tc_SPLower = (APTR)((BYTE*)newTask + ((sizeof(struct Task) + 3) & 0xFFFFFFFC));
#else
    /*
     * This will allocate two chunks of memory: task of PUBLIC
     * and stack of PRIVATE
     */
    struct FakeMemList fakememlist;
    struct MemList *ml;
    fakememlist = TaskMemTemplate;
    fakememlist.fml_ME[ME_STACK].fme_Length = stackSize;

    ml = (struct MemList *) AllocEntry( (struct MemList *)&fakememlist );

    /* NOTE ! - AllocEntry returns with bit 31 set if it fails ! */
    if( ((ULONG)ml) & 0x80000000 ) {
      D(("MyCreateTask: AllocEntry failed!\n"));
    	return( NULL );
    }

    /* set the stack accounting stuff */
    newTask = (struct Task *) ml->ml_ME[ME_TASK].me_Addr;

    newTask->tc_SPLower = ml->ml_ME[ME_STACK].me_Addr;
#endif
    newTask->tc_SPUpper = (APTR)(((ULONG)(newTask->tc_SPLower) + ((stackSize - 2) & 0xFFFFFFFC)) & 0xFFFFFFFE);
    newTask->tc_SPReg = newTask->tc_SPUpper;
    char* nameInRAM = (char*)((BYTE*)newTask->tc_SPUpper + ((stackSize + 3) & 0xFFFFFFFC));
    strcpy(nameInRAM, name);

    /* misc task data structures */
    newTask->tc_Node.ln_Type = NT_TASK;
    newTask->tc_Node.ln_Pri = pri;
    newTask->tc_Node.ln_Name = nameInRAM;
    newTask->tc_UserData = taskUserData;

    /* add it to the tasks memory list */
    NewList( &newTask->tc_MemEntry );
    AddHead( &newTask->tc_MemEntry, (struct Node*)ml );

    /* add the task to the system -- use the default final PC */
    AddTask( newTask, initPC, 0 );
    return( newTask );
}

// not available in 1.3
static struct MsgPort* MyCreateMsgPort(struct DevBase *base) {
  struct MsgPort *mp = NULL;

#if 1
  //D(("CreateMsgPort: before AllocSignal\n"));
  BYTE signal = AllocSignal(-1);
  if(signal == -1) {
    D(("MyCreateMsgPort: NO SIGNAL!\n"));
  }
  else
  {
    //D(("CreateMsgPort: before AllocMem\n"));
    mp = (struct MsgPort *)AllocMem(sizeof(*mp), MEMF_PUBLIC|MEMF_CLEAR);
    if(!mp) {
      D(("MyCreateMsgPort: NO MEMORY!\n"));
      FreeSignal(signal);
    }
    else {
      mp->mp_SigBit = signal;
      mp->mp_Node.ln_Type = NT_MSGPORT;
      mp->mp_Flags = PA_SIGNAL;
      //mp->mp_SigTask = ((struct ExecBase*)base->sysBase)->ThisTask;
      mp->mp_SigTask = FindTask(NULL);
      NewList(&mp->mp_MsgList);
      mp->mp_MsgList.lh_Type = NT_MESSAGE;
    }
  }
  #else
    D(("MyCreateMsgPort: before CreateMsgPort\n"));
    mp = CreateMsgPort();
    D(("MyCreateMsgPort: after CreateMsgPort\n"));
  #endif

  return mp;
}

// not available in 1.3
static void MyDeleteMsgPort(struct DevBase *base, struct MsgPort* mp) {
#if 1
  D(("MyDeleteMsgPort: before FreeSignal\n"));
  // TODO: check what the original function does (e.g. replying all messages in port?)
  FreeSignal(mp->mp_SigBit);
  FreeMem(mp, sizeof(*mp));
#else
    D(("MyDeleteMsgPort: before DeleteMsgPort\n"));
    mp = DeleteMsgPort(mp);
    D(("MyDeleteMsgPort: after DeleteMsgPort\n"));
#endif
}


static SAVEDS ASM void worker_main(void)
{
  struct IOStdReq *ior;
  struct MsgPort *port;

  /* retrieve dev base stored in user data of task */
  struct InitData *id = worker_startup();
  struct DevBase *base = id->base;
  D(("Task: id=%08lx base=%08lx base->sysBase=%08lx\n", id, base,  base->sysBase));

  /* create worker port */
  port = MyCreateMsgPort(base);
  D(("Port: %08lx\n", port));

  /* call user init */
  if(port != NULL) {
    if(!mydev_worker_init(base)) {
      /* user aborted worker */
      D(("mydev_worker_init failed\n"));
      MyDeleteMsgPort(base, port);
      port = NULL;
    }
  }


  //Guru Information:
  //http://www.bambi-amiga.co.uk/amigahistory/guruguide.html

  /* setup port or NULL and trigger signal to caller task */
  base->workerPort = port;
  D(("Task: signal task=%08lx mask=%08lx\n", id->initTask, id->initSigMask));
  Signal(id->initTask, id->initSigMask);

  /* only if port is available then enter work loop. otherwise quit task */
  if(port != NULL)
  {
    /* worker loop */
    D(("Task: enter\n"));
    BOOL stay = TRUE;
    while (stay) {
      D(("Task: WaitPort\n"));
      WaitPort(port);
      while (1) {
        ior = (struct IOStdReq *)GetMsg(port);
        D(("Task: ior=%08lx\n", ior));
        if(ior == NULL) {
          break;
        }
        /* terminate? */
        if(ior->io_Command == CMD_TERM) {
          stay = FALSE;
          ReplyMsg(&ior->io_Message);
          break;
        }
        /* regular command */
        else {
          mydev_worker_cmd(base, ior);
          ReplyMsg(&ior->io_Message);
        }
      }
    }

    /* call shutdown only if worker was entered */
    D(("Task: exit\n"));
    /* shutdown worker */
    mydev_worker_exit(base);
  }

  D(("Task: delete port\n"));
  MyDeleteMsgPort(base, port);
  base->workerPort = NULL;

  /* kill myself */
  D(("Task: die\n"));
  struct Task *me = FindTask(NULL);
  DeleteTask(me);
  Wait(0);
  D(("Task: NEVER!\n"));
}

BOOL worker_start(struct DevBase *base)
{
  D(("Worker: start\n"));
  base->workerPort = NULL;

  /* alloc a signal */
  BYTE signal = AllocSignal(-1);
  if(signal == -1) {
    D(("Worker: NO SIGNAL!\n"));
    return FALSE;
  }

  /* setup init data */
  struct InitData id;
  id.initSigMask = 1 << signal;
  id.initTask = FindTask(NULL);
  id.base = base;
  D(("Worker: init data %08lx\n", &id));

  /* now launch worker task and inject dev base
     make sure worker_main() does not run before base is set.
  */
  //Forbid();
  struct Task *myTask = MyCreateTask(base, &id, WorkerTaskName, 0, (const APTR)worker_main, 4096UL);
  if(myTask != NULL) {
    //myTask->tc_UserData = (APTR)&id;
  }
  //Permit();
  if(myTask == NULL) {
    D(("Worker: NO TASK!\n"));
    FreeSignal(signal);
    return FALSE;
  }

  /* wait for start signal of new task */
  D(("Worker: wait for task startup. sigmask=%08lx\n", id.initSigMask));
  Wait(id.initSigMask);

  FreeSignal(signal);

  /* ok everything is fine. worker is ready to receive commands */
  D(("Worker: started: port=%08lx\n", base->workerPort));
  return ((base->workerPort != NULL) ? TRUE : FALSE);
}

void worker_stop(struct DevBase *base)
{
  struct IORequest newior;

  D(("Worker: stop\n"));

  if(base->workerPort != NULL) {
    /* send a message to the child process to shut down. */
    newior.io_Message.mn_ReplyPort = MyCreateMsgPort(base);
    newior.io_Command = CMD_TERM;

    /* send term message and wait for reply */
    PutMsg(base->workerPort, &newior.io_Message);
    WaitPort(newior.io_Message.mn_ReplyPort);
    MyDeleteMsgPort(base, newior.io_Message.mn_ReplyPort);
  }

  D(("Worker: stopped\n"));
}
