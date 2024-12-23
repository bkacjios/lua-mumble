#include "mumble.h"

#include "thread.h"

typedef struct ThreadNode ThreadNode;
struct ThreadNode
{
	pthread_t thread;
	struct ThreadNode *next;
};

typedef struct ClientThread ClientThread;
struct ClientThread
{
	MumbleThread *thread;
};

static ThreadNode* mumble_threads;

enum { READ = 0, WRITE = 1 };

void thread_add(ThreadNode** head_ref, pthread_t thread)
{
	ThreadNode* new_node = malloc(sizeof(ThreadNode));

	new_node->thread = thread;
	new_node->next = (*head_ref);

	(*head_ref) = new_node;
}

void thread_remove(ThreadNode **head_ref, pthread_t thread)
{
	ThreadNode* temp = *head_ref, *prev;

	// If head node itself holds the key to be deleted
	if (temp != NULL && temp->thread == thread)
	{
		*head_ref = temp->next;   // Changed head
		free(temp);               // free old head
		return;
	}

	// Search for the key to be deleted, keep track of the
	// previous node as we need to change 'prev->next'
	while (temp != NULL && temp->thread != thread)
	{
		prev = temp;
		temp = temp->next;
	}

	// If key was not present in linked list 
	if (temp == NULL) return;

	// Unlink the node from linked list 
	prev->next = temp->next;

	free(temp);               // free old head
}

static void *mumble_thread_worker(void *arg)
{
	MumbleThread *thread = arg;

	// Create state and load libs
	lua_State *l = luaL_newstate();

	lua_stackguard_entry(l);
	
	luaL_openlibs(l);

	// mumble_init(l);
	// lua_getfield(l, -1, "thread");
	// lua_remove(l, -2);

	// ClientThread *cthread = lua_newuserdata(l, sizeof(ClientThread));
	// cthread->thread = thread;
	// luaL_getmetatable(l, METATABLE_THREAD_CLIENT);
	// lua_setmetatable(l, -2);
	// lua_setfield(l, -2, "worker");
	// lua_pop(l,1);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Load the file in our thread
	int err = luaL_loadfile(l, thread->filename);

	// Call the worker with our custom error handler function
	if (err > 0 || lua_pcall(l, 0, 0, -2) != 0) {
		mumble_log(LOG_ERROR, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	lua_stackguard_exit(l);

	// Close state
	lua_close(l);

	// Signal the main Lua stack that our thread has completed
	write(thread->pipe[WRITE], &thread->finished, sizeof(thread->finished));

	return NULL;
}

void mumble_thread_event(struct ev_loop *loop, ev_io *w_, int revents)
{
	thread_io *w = (thread_io *) w_;

	// TODO: Allow sending and receiving of data between threads using this pipe
	int finished;
	int n = read(w_->fd, &finished, sizeof(int));

	if (n > 0) {
		mumble_log(LOG_DEBUG, "thread event data received\n");
		mumble_thread_exit(w->l, w->thread);
	} else {
		mumble_log(LOG_ERROR, "thread pipe error\n");
	}
}

int mumble_thread_exit(lua_State *l, MumbleThread *thread)
{
	lua_stackguard_entry(l);

	// Check if we have a finished callback reference
	if (thread->finished > 0) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		// Push the worker function from the registry
		mumble_registry_pushref(l, MUMBLE_THREAD_REG, thread->finished);

		// Call the worker with our custom error handler function
		if (lua_pcall(l, 0, 0, -2) != 0) {
			mumble_log(LOG_ERROR, "%s: %s\n", METATABLE_THREAD_SERVER, lua_tostring(l, -1));
			lua_pop(l, 1); // Pop the error
		}

		// Pop the error handler
		lua_pop(l, 1);

		mumble_registry_unref(l, MUMBLE_THREAD_REG, thread->finished);
	}

	mumble_registry_unref(l, MUMBLE_TIMER_REG, thread->self);
	thread_remove(&mumble_threads, thread->pthread);

	lua_stackguard_exit(l);
}

int mumble_thread_new(lua_State *l)
{
	MumbleThread *thread = lua_newuserdata(l, sizeof(MumbleThread));

	thread->filename = luaL_checkstring(l, 2);
	thread->finished = 0;

	if (pipe(thread->pipe) != 0) {
		return luaL_error(l, "could not create thread pipe");
	}

	lua_pushvalue(l, 1); // Push a copy of the userdata to prevent garabage collection
	thread->self = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference

	if (lua_isfunction(l, 3)) {
		lua_pushvalue(l, 3); // Push a copy of our callback function
		thread->finished = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	luaL_getmetatable(l, METATABLE_THREAD_SERVER);
	lua_setmetatable(l, -2);

	thread->event.l = l;
	thread->event.thread = thread;

	ev_io_init(&thread->event.io, mumble_thread_event, thread->pipe[READ], EV_READ);
	ev_io_start(EV_DEFAULT, &thread->event.io);

	pthread_create(&thread->pthread, NULL, mumble_thread_worker, thread);

	thread_add(&mumble_threads, thread->pthread);
	
	return 1;
}

void mumble_thread_join_all()
{
	ThreadNode* current = mumble_threads;

	while (current != NULL)
	{
		pthread_join(current->thread, NULL);
		current = current->next;
	}
}

static int thread_server_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_SERVER, lua_topointer(l, 1));
	return 1;
}

static int thread_server_gc(lua_State *l)
{
	MumbleThread *thread = luaL_checkudata(l, 1, METATABLE_THREAD_SERVER);
	ev_io_stop(EV_DEFAULT, &thread->event.io);
	close(thread->pipe[READ]);
	close(thread->pipe[WRITE]);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_THREAD_SERVER, thread);
	return 0;
}

const luaL_Reg mumble_thread_server[] = {
	{"__tostring", thread_server_tostring},
	{"__gc", thread_server_gc},
	{NULL, NULL}
};

static int thread_client_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_CLIENT, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_thread_client[] = {
	{"__tostring", thread_client_tostring},
	{NULL, NULL}
};