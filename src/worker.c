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

static ULONG mystrlen(const char* str) {
  ULONG len = 0;
  while(str[len++]) {}  

  return len - 1;
}

static void mystrcpy(char* strdst, const char* strsrc) {
  ULONG len = 0;
  while(*(strdst++) = *(strsrc++)) {}
}

static struct Task * MyCreateTask(struct DevBase *base, void* taskUserData, const char *name, BYTE pri, const APTR initPC, ULONG stackSize)
{
    struct Task *newTask = NULL;

    const UWORD numChunks = 1;
    const ULONG mlAllocSize = sizeof(struct MemList) + sizeof(struct MemEntry) * (numChunks - 1);
    struct MemList *ml = (struct MemList *) AllocMem(mlAllocSize, MEMF_PUBLIC | MEMF_CLEAR);
    if(!ml) {
      D(("MyCreateTask: AllocMem for MemList failed!\n"));
    	return( NULL );
    }

    /*
     * This will allocate one chunk of memory which contains 3 parts:
     *  1. task structure
     *  2. stack
     *  3. name
     */
    const ULONG taskAllocSize = ((sizeof(struct Task) + 3) & 0xFFFFFFFC); // round up to next 4 bytes
    stackSize = (stackSize + 3) & 0xFFFFFFFC;
    const ULONG nameAllocSize = ((mystrlen(name) + 1 + 3) & 0xFFFFFFFC); // round up to next 4 bytes
    const ULONG wholeAllocSize = taskAllocSize + stackSize + nameAllocSize;
    UBYTE *chunk = (UBYTE *) AllocMem(wholeAllocSize, MEMF_PUBLIC | MEMF_CLEAR);
    if(!chunk) {
      D(("MyCreateTask: AllocMem for chunk failed!\n"));
      FreeMem(ml, mlAllocSize);
    	return( NULL );
    }
    ml->ml_NumEntries = numChunks;
    ml->ml_ME[0].me_Length = wholeAllocSize;
    ml->ml_ME[0].me_Un.meu_Addr = (APTR)chunk;

    //newTask = (struct Task *) ml->ml_ME[ME_TASK].me_Addr;
    //newTask->tc_SPLower = (BYTE*)ml->ml_ME[ME_TASK].me_Addr + ((sizeof(struct Task) + 3) & 0xFFFFFFFC);
    //ml->ml_ME[ME_TASK].me_Addr = (APTR)((BYTE*)ml + ((sizeof(struct FakeMemList) + 3) & 0xFFFFFFFC));
    newTask = (struct Task *)chunk;
    newTask->tc_SPLower = (APTR)(chunk + taskAllocSize);
    char* nameInRAM = (char*)(chunk + taskAllocSize + stackSize);
    mystrcpy(nameInRAM, name);
    newTask->tc_SPUpper = (APTR)((UBYTE*)(newTask->tc_SPLower) + (((stackSize - 2) & 0xFFFFFFFC) & 0xFFFFFFFE));
    newTask->tc_SPReg = newTask->tc_SPUpper;

    /* misc task data structures */
    newTask->tc_Node.ln_Type = NT_TASK;
    newTask->tc_Node.ln_Pri = pri;
    newTask->tc_Node.ln_Name = nameInRAM;
    newTask->tc_UserData = taskUserData;

    /* add it to the tasks memory list */
    NewList( &newTask->tc_MemEntry );
    AddHead( &newTask->tc_MemEntry, &ml->ml_Node );

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
    D(("CreateMsgPort: SignalMask=%lx\n", (ULONG)(1<<signal)));
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
  D(("Task: id=%08lx base=%08lx base->sysBase=%08lx\n", id, base, base->sysBase));

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
  // after this line 'id' cannot be used any longer because it is freed in the caller (lies on its stack)

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

  struct Task *myTask = MyCreateTask(base, &id, WorkerTaskName, 0, (const APTR)worker_main, 4096UL);
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
