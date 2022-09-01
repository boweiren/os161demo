/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>

#include <synch.h>
//#include <stdlib.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

/* Use a lock to protect the number of left ropes */
static struct lock *leftLock;

/* Data structures for rope mappings */

/* Implement this! */

/* Define a rope structure for rope mapping, containing its stake, hook and status. */
typedef struct rope{
    struct lock *ropeLock;
    volatile int stake;
    // no need to define a hook since the rope does not switch hooks
    //volatile int hook;
    volatile bool cut;
} rope;

/* Create a list containing all the ropes */
static rope ropeList[NROPES-1];

/* Synchronization primitives */

/* Implement this! */
int cv;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */
    while (ropes_left > 0) {
        // Select a random hook to sever
        int currHook = random() % NROPES;
        
        if (!ropeList[currHook].cut) {
            lock_acquire(ropeList[currHook].ropeLock);
            // check again in case it is modified before lock_acquire
            if (!ropeList[currHook].cut) {
                ropeList[currHook].cut = true;

                lock_acquire(leftLock);
                ropes_left--;
                lock_release(leftLock);

                kprintf("Dandelion severed rope %d\n", currHook);
            }
            lock_release(ropeList[currHook].ropeLock);
            thread_yield();
        }
    }

    kprintf("Dandelion thread done\n");
    thread_exit();
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
    while (ropes_left > 0) {

        // Select a random stake to untie
        int currStake = random() % NROPES;

        // Find the stake
        for (int i = 0; i < NROPES; i++) {
            if (ropeList[i].stake == currStake && !ropeList[i].cut) {
                // Lock current rope
                lock_acquire(ropeList[i].ropeLock);

                // Check again in case the stake number switched just before require lock
                if (ropeList[i].stake == currStake && !ropeList[i].cut) {
                    ropeList[i].cut = true;

                    lock_acquire(leftLock);
                    ropes_left--;
                    lock_release(leftLock);

                    kprintf("Marigold severed rope %d from stake %d\n", i, ropeList[i].stake);
                } 

                lock_release(ropeList[i].ropeLock);
                thread_yield();
            }  
        }
    }

    kprintf("Marigold thread done\n");
    thread_exit();
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

	/* Implement this function */

    // There should be more than two ropes to perform switch
    while (ropes_left > 1) {
        // Find random stakes
        int currStake = random() % NROPES;

        for (int i = 0; i < NROPES; i++) {
            if (ropeList[i].stake == currStake) {
                lock_acquire(ropeList[i].ropeLock);
                // check again in case other FlowerKiller or Marigold modified the stake
                if (ropeList[i].stake == currStake && !ropeList[i].cut) {
                    int targetRope = random() % NROPES;

                    while (targetRope == i) {
                        targetRope = random() % NROPES;
                    }
                    lock_acquire(ropeList[targetRope].ropeLock);
                    if (!ropeList[targetRope].cut) {
                        int tmp1 = ropeList[i].stake;
                        int tmp2 = ropeList[targetRope].stake;
                        ropeList[i].stake = ropeList[targetRope].stake;
                        kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", i, tmp1, tmp2);
                        ropeList[targetRope].stake = tmp1;
                        kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", targetRope, tmp2, tmp1);
                    }
                    lock_release(ropeList[targetRope].ropeLock);
                    
                    /*
                    int targetStake = random() % NROPES;
                    if (currStake != targetStake) {
                        for (int j = 0; j < NROPES; j++) {
                            if (ropeList[j].stake == currStake && !ropeList[j].cut) {
                                lock_acquire(ropeList[j].ropeLock);
                                if (ropeList[j].stake == currStake && !ropeList[j].cut) {
                                    int tmp = ropeList[i].stake;
                                    ropeList[i].stake = ropeList[j].stake;
                                    kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", i, currStake, targetStake);
                                    ropeList[j].stake = tmp;
                                    kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", j, targetStake, currStake);
                                }
                                lock_release(ropeList[j].ropeLock);
                            }
                        }
                    }
                    */
                }
                lock_release(ropeList[i].ropeLock);
                thread_yield();
            }
        }
    }

    kprintf("Lord FlowerKiller thread done\n");
    thread_exit();
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */
    while (1) {
        if (ropes_left == 0) {
            break;
        }
        thread_yield();
    }
    kprintf("Balloon freed and Prince Dandelion escapes!\n");

    cv = 1;

    kprintf("Balloon thread done\n");

    thread_exit();
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;
    cv = 0;

	(void)nargs;
	(void)args;
	(void)ropes_left;

    leftLock = lock_create("ropes_left_lock");

    // Initialize ropes
    for (i = 0; i < NROPES; i++) {
        ropeList[i].ropeLock = lock_create("");
        ropeList[i].stake = i;
        ropeList[i].cut = false;
    }

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

    while (cv == 0) {
        thread_yield();
    }

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
    lock_destroy(leftLock);

    for (i = 0; i < NROPES; i++) {
        lock_destroy(ropeList[i].ropeLock);
    }

    thread_yield();
    kprintf("Main thread done\n");

	return 0;
}
