/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

#define FK 0

/* Data structures for rope mappings */

/* 
 * This array of stakes is accessed only 
 * by Marigold and Lord FlowerKillers. 
 */
static struct stake *stakes[NROPES];

/* This array of hooks can only be accessed by Dandelion */
static struct hook *hooks[NROPES];

/* This array of ropes can be accessed via hooks and stakes*/
static struct rope *ropes[NROPES];

/* Each hook holds the index to the rope array */
struct hook {
	int rope_index;
};

/* 
 * Each stake holds the index to the rope array. A stake has
 * a lock as Marigold and FlowerKillers will be acquiring the stakes.
 */
struct stake {
	int rope_index;
	struct lock *stake_lock;
};

/*
 * Each rope is either attached or not indicated by is_cut.
 * 
 * Each rope has a rope_index which represents where
 * in the ropes array it is positioned.
 * 
 * Ropes are accessed by Dandelion, Marigold and Lord FlowerKillers,
 * which implies they should be protected by a lock to ensure
 * mutual exclusion when entering critical sections.
 */
struct rope {
	bool is_cut;
	int rope_number;
	struct lock *rope_lock;
};


/* Synchronization primitives */

/*
 * The lock to ensure mutual exclusion when reading or writing
 * to the ropes_left global variable.
 */
static struct lock *ropes_left_lock;

/* A semaphore used to ensure proper order of prints. */
static struct semaphore *main_sem;

/* A semaphore used to ensure balloon thread doesn't busy wait. */
static struct semaphore *balloon_sem;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 * 
 * Below is a high level description of my design.
 * 
 * Dandelion:
 * 
 * To ensure requirements are met, Dandelion can only access ropes
 * via hooks. The global array, hooks, contains NROPES number 
 * of hook structures. Each hook structure contains an index
 * to a global array of ropes called ropes. Whenever Dandelion
 * wants to unhook a rope, he will choose a random hook, see
 * if the rope attahced to it is already unhook or severed at
 * the stake. If the rope is not attached (severed at the stake or unhooked),
 * he will move onto another hook. If the rope is not attached,
 * he will unhook it from the hot air balloon. My design assumes
 * Marigold and Lord FlowerKillers will not access ropes via hooks.
 * Dandelion exits once ropes_left is equal to 0. 
 * This condition is checked every iteration of his functionality.
 * 
 * Marigold:
 * 
 * Marigold can only access ropes via stakes. Marigold generates 
 * a random stake, checks if there is a rope attached, and severs
 * the rope if it isn't already. My design assumes Marigold and 
 * Lord FlowerKillers will be trying to access ropes via stakes 
 * which is why each stake has a lock associated with it. Whenever
 * Marigold accesses a stake, she aqcuires it's lock. Marigold will
 * then access the rope's lock associated with the stake. If the
 * rope is severed or unhooked, she will move onto another stake
 * and release both locks. Marigold exits once ropes_left is 
 * equal to 0. This condition is checked every iteration
 * of her functionality.
 * 
 * Lord FlowerKiller:
 * 
 * Lord FlowerKillers access ropes via stakes. To ensure there are
 * no deadlocks, I have implemented a strict lock access pattern
 * in my design. Each Lord FlowerKiller will aqcuire the lock 
 * associated with the lower indexed stake first. Once Lord
 * FlowerKiller successfully acquires both stakes and ropes, he 
 * will swap the ropes at the stakes. The Lord FlowerKiller 
 * threads exit once ropes_left is equal to 0 which is checked 
 * every iteration.
 * 
 * ropes_left_lock:
 * 
 * The global variable ropes_left is accessed by multiple threads.
 * In order to avoid race conditions, the ropes_left_lock should 
 * be acquired everytime the ropes_left variable is either decremented
 * or it's value read from memory.
 * 
 * main_sem:
 * 
 * This is a semaphore used to make sure the final print statement 
 * will be printed last. Once each thread is finished, they call
 * the V function to increment the semaphore count.
 * 
 * balloon_sem:
 * 
 * This is a semaphore used to make sure the final the balloon
 * thead doesn't busy wait. The balloon thread will exit once
 * all the ropes are severed/unhooked.
 * 
 */

/* 
 * Create and return rope with initial rope_index
 * rope_index >= 0 
 * rope_index < NROPES
 */
static
struct 
rope *create_rope(int rope_index) 
{
	KASSERT(rope_index >= 0);
	KASSERT(rope_index < NROPES);

	struct rope *r;
	struct lock *l;

	l = lock_create("rope lock");

	r = kmalloc(sizeof(struct rope));

	r->is_cut = false;
	r->rope_number = rope_index;
	r->rope_lock = l;

	return r;
}

/* 
 * Create and return a stake with initial rope_index
 * rope_index >= 0 
 * rope_index < NROPES
 */
static
struct 
stake *create_stake(int rope_index)
{
	KASSERT(rope_index >= 0);
	KASSERT(rope_index < NROPES);

	struct stake *s;
	struct lock *l;

	l = lock_create("stake lock");

	s = kmalloc(sizeof(struct stake));
	s->rope_index = rope_index;
	s->stake_lock = l;

	return s;
}

/* 
 * Create and return a hook with initial rope_index
 * rope_index >= 0 
 * rope_index < NROPES
 */
static
struct 
hook *create_hook(int rope_index) 
{
	KASSERT(rope_index >= 0);
	KASSERT(rope_index < NROPES);

	struct hook *h;

	h = kmalloc(sizeof(struct hook));
	h->rope_index = rope_index;

	return h;
}

/* 
 * Destroy a hook given a hook_index
 * hook_index >= 0 
 * hook_index < NROPES
 */
static
void 
destroy_hook(int hook_index)
{
	KASSERT(hook_index >= 0);
	KASSERT(hook_index < NROPES);

	kfree(hooks[hook_index]);
}

/* 
 * Destroy a stake given a stake_index
 * stake_index >= 0 
 * stake_index < NROPES
 */
static
void 
destroy_stake(int stake_index)
{
	KASSERT(stake_index >= 0);
	KASSERT(stake_index < NROPES);

	lock_destroy(stakes[stake_index]->stake_lock);
	kfree(stakes[stake_index]);
}

/* Destroy a rope object at a given rope_index */
static
void 
destroy_rope(int rope_index)
{
	KASSERT(rope_index >= 0);
	KASSERT(rope_index < NROPES);

    lock_destroy(ropes[rope_index]->rope_lock);
	kfree(ropes[rope_index]);
}

/* 
 * Initialize the ropes, stakes and hooks.
 * Each rope starts with a 1:1 mapping.
 */
static
void 
init_balloon()
{
	int rope_idx;

	for (rope_idx = 0; rope_idx < NROPES; rope_idx++){
		stakes[rope_idx] = create_stake(rope_idx);
		hooks[rope_idx] = create_hook(rope_idx);
		ropes[rope_idx] = create_rope(rope_idx);
	}
	ropes_left_lock = lock_create("ropes left lock");
	return;
}

/* Destroy a rope object at a given rope_index */
static
void 
destroy_balloon()
{
	int index;

	for (index = 0; index < NROPES; index++){
		destroy_hook(index);
		destroy_stake(index);
		destroy_rope(index);
	}

	lock_destroy(ropes_left_lock);
	return;
}


static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	int rand = 0;
	struct stake *cur_stake;
	struct rope *cur_rope;

	kprintf("Marigold thread starting\n");

	while (1) {

		/* If there are no ropes left, no more work to do */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0){
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		rand = random() % NROPES;

		/* Aquire the stake lock */
		lock_acquire(stakes[rand]->stake_lock);
		cur_stake = stakes[rand];

		/* Aquire the lock of the rope associated to the stake */
		lock_acquire(ropes[cur_stake->rope_index]->rope_lock);
		cur_rope = ropes[cur_stake->rope_index];

		/* Sever the rope if it is still attached */
		if (!cur_rope->is_cut) {
			cur_rope->is_cut = true;

			/* Atomically update the number of ropes left */
			lock_acquire(ropes_left_lock);

			ropes_left--;

			kprintf("Marigold severed rope %d from stake %d\n", cur_stake->rope_index, rand);

			V(balloon_sem);

			lock_release(ropes_left_lock);	

			/* Allow other threads to run */
			lock_release(cur_stake->stake_lock);
			lock_release(cur_rope->rope_lock);
			thread_yield();

		} else {
			/* The rope is not attached, move onto another stake */
			lock_release(cur_stake->stake_lock);
			lock_release(cur_rope->rope_lock);
			continue;
		}
	}
	
		kprintf("Marigold thread done\n");
		V(main_sem);
		thread_exit();
}
static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	int rand = 0;
	struct hook *cur_hook;
	struct rope *cur_rope;

	kprintf("Dandelion thread starting\n");

	while(1) {

		/* If there are no ropes left, no more work to do */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0){
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		/*
		*	Dandelion selects a random hook.
		*	If the rope is not attached at cur_hook, move onto another hook
		*/
		rand = random() % NROPES;
		cur_hook = hooks[rand];
		cur_rope = ropes[cur_hook->rope_index];

		lock_acquire(cur_rope->rope_lock);
		/* If the rope is not attached, move onto another one */
		if (cur_rope->is_cut){
			lock_release(cur_rope->rope_lock);
			continue;
		} else {
			/* The rope is still attached. Dandelion unhooks it */
			cur_rope->is_cut = true;

			/* Atomically update the number of ropes left */
			lock_acquire(ropes_left_lock);

			ropes_left--;

			kprintf("Dandelion severed rope %d\n", cur_rope->rope_number);

			V(balloon_sem);

			lock_release(ropes_left_lock);

			/* Allow other threads to run */
			lock_release(cur_rope->rope_lock);
			thread_yield();
		}	
	}
	kprintf("Dandelion thread done\n");
	V(main_sem);
	thread_exit();
}


/*
 * Flowerkiller finds a stake that isn't cut and aquires its lock
 * at the same time, he will aquire the flowerkillers_are_holding[] lock + update it
 * if the rope has been cut while he was aquiring the lock, move on and update the boolean array
 * if the rock hasnt been cut, attempt to aquire another stake randomly that will be swapped
 * update the boolean array 
 * yeild
 */
static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	int first_stake_index = 0;
	int second_stake_index = 0;
	int r1 = 0;
	int r2 = 0;
	struct stake *first_stake;
	struct stake *second_stake;
	struct rope *first_rope;
	struct rope *second_rope;
	int temp = 0;

	kprintf("Lord FlowerKiller thread starting\n");


	while(1) {

		/* If there are no ropes left, no more work to do */
		lock_acquire(ropes_left_lock);
		if (ropes_left == 0){
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);

		r1 = random() % NROPES;
		r2 = random() % NROPES;

		/* If the two random numbers are the same (same stake), move on. */
		if (r1 == r2){
			continue;
		} 

		/*
		 * Following a strict lock aquirement schedule. FlowerKiller will
		 * always select the lower random index first to avoid deadlocks.
		 */
		first_stake_index = r1 < r2 ? r1 : r2;
		second_stake_index = r1 < r2 ? r2 : r1;


		/* 
		 * Acquire the lock of first stake lock so Marigold
		 * or another Flowerkiller can't touch it. 
		 */
		lock_acquire(stakes[first_stake_index]->stake_lock);
		first_stake = stakes[first_stake_index];

		/* 
		 * Acquire the lock of the first rope so Dandelion,
		 * another FlowerKiller or marigold can't touch it. 
		 */
		lock_acquire(ropes[first_stake->rope_index]->rope_lock);
		first_rope = ropes[first_stake->rope_index];

		/*
		 * If the first rope is attached, proceed with algorithm
		 * otherwise, release both aqcuired locks and start over.
		 */
		if (!first_rope->is_cut) {

			/* 
			 * Acquire the lock of second stake lock so 
			 * Marigold or another Flowerkiller can't touch it.
			 */			
			lock_acquire(stakes[second_stake_index]->stake_lock);
			second_stake = stakes[second_stake_index];

			/* 
			 * Acquire the lock of the first rope so Dandelion,
			 * Marigold or another FlowerKiller can't touch it.
			 */
			lock_acquire(ropes[second_stake->rope_index]->rope_lock);
			second_rope = ropes[second_stake->rope_index];
			
			/* 
			 * If the second rope is attached FlowerKiller will 
			 * proceed with swapping ropes at the stakes.
			 */ 
			if (!second_rope->is_cut) {
				
				/* Perform swap */

				temp = stakes[first_stake_index]->rope_index;
				stakes[first_stake_index]->rope_index = stakes[second_stake_index]->rope_index;
				stakes[second_stake_index]->rope_index = temp;


				kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\nLord FlowerKiller switched rope %d from stake %d to stake %d\n",
				first_rope->rope_number, first_stake_index, second_stake_index, second_rope->rope_number, second_stake_index, first_stake_index);			

				/* Release all locks as FlowerKiller leaves the critical section */
				lock_release(first_stake->stake_lock);
				lock_release(first_rope->rope_lock);	
				lock_release(second_stake->stake_lock);
				lock_release(second_rope->rope_lock);

				/* Allow other threads to run */
				thread_yield();

			} else {
				lock_release(first_stake->stake_lock);
				lock_release(first_rope->rope_lock);	
				lock_release(second_stake->stake_lock);
				lock_release(second_rope->rope_lock);
				continue;
			}
		} else {
			lock_release(first_stake->stake_lock);
			lock_release(first_rope->rope_lock);
			continue;
		}
	}
	kprintf("Lord FlowerKiller thread done\n");
	V(main_sem);
	thread_exit();
}
static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	int i;

	balloon_sem = sem_create("balloon semaphore", 0);

	kprintf("Balloon thread starting\n");

	/* 
	 * So the balloon isn't busy waiting, wait for
	 * the other threads to complete
	 */
	for (i = 0; i < NROPES; i++)
		P(balloon_sem);

	kprintf("Balloon freed and Prince Dandelion escapes!\n");

	kprintf("Balloon thread done\n");

	V(main_sem);
	thread_exit();
}


int
airballoon(int nargs, char **args)
{

	int err = 0, i;
	(void)i;

	(void)nargs;
	(void)args;
	ropes_left = NROPES;

	main_sem = sem_create("print main_sem", 0);

	/* Initialize ropes, stakes and hooks */
	init_balloon();

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;
	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;
	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	/* To ensure the main thread prints after all others are complete */
	int idx;
	for (idx = 0; idx < N_LORD_FLOWERKILLER +  3; idx++){
		P(main_sem);
	}

	/* De-allocate all memory */
	destroy_balloon();

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	kprintf("Main thread done\n");
	return 0;
}
