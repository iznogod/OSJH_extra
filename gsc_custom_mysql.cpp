/************************************************************
 * Filename: gsc_mysql.cpp                                  *
 * Description: Contains asynchronous MySQL functionality   *
                and functions that can be called from GSC   *
 ************************************************************/


/* Includes */
#include <mysql/mysql.h>
#include <pthread.h>
#include <unistd.h>
#include "gsc_custom_mysql.hpp"

/* Defines */
#define  MYSQL_NO_ERROR         0
#define  MYSQLA_TASK_BUSY       0

/* Typedefs */
typedef struct mysqla_task
{
    int taskId;                 // ID of the task
    struct mysqla_task *prev;   // Previous linked list entry
    struct mysqla_task *next;   // Next linked list entry
    MYSQL_RES *result;          // MySQL resulting rows of the task's query
    gentity_t *entity;          // The entity upon which this query was called (or NULL)
    bool entityDisconnected;    // Whether the entity has disconnected since the task was scheduled
    bool done;                  // Whether or not the task is finished
    bool started;               // Whether or not the task has started
    bool save;                  // Whether or not the result will be saved
    char query[1024 + 1];  // The (to be) executed query
} mysqla_task_t; // Allocate this dynamically due to 1024 chars being reserved

typedef struct mysqla_connection
{
    struct mysqla_connection *prev; // Previous linked list entry
    struct mysqla_connection *next; // Next linked list entry
    mysqla_task_t *task; // Each connection can have multiple tasks
    MYSQL *connection;   // The actual MySQL connection
} mysqla_connection_t;

//typedef void (*mysql_result_callback_t)(int id, unsigned int result);


/* Global variables */
static mysqla_connection_t  *first_async_connection; // Pointer to first connection (start of linked list)
static mysqla_task_t        *first_async_task;       // Pointer to first task (start of linked list)
static MYSQL                *sync_mysql_connection;
static pthread_mutex_t       mysqla_lock;
static pthread_mutex_t       mysqla_file_lock;

//static mysql_result_callback_t mysql_result_callback;
static int mysql_result_callback;


/* Const variables */



/* Local functions */



/* Public functions */

/************************************************************
 *              Functions !NOT! callable from GSC           *
 ************************************************************/
 
/*
 * Push all fields of all rows from result to the GSC caller
 */
static void pushResultRows(MYSQL_RES *result)
{
    stackPushArray();
    
    int num_rows = mysql_num_rows(result);
    for(int i = 0; i < num_rows; i++)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        
        stackPushArray();
        
        int num_fields = mysql_num_fields(result);
        for(int j = 0; j < num_fields; j++)
        {
            if(row[j])
                stackPushString(row[j]);
            else
                stackPushUndefined();
            
            stackPushArrayLast();
        }
        
        stackPushArrayLast();
    }
}
 
/*
 * Call the result callback for each finished MySQL task.
 * Note: This is called from onFrame function by the server.
 *       If this starts lagging, add a break statement after calling a callback.
 */
void mysql_handle_result_callbacks(void)
{
    // Ensure we have a callback function active
    if(mysql_result_callback == 0)
        return;
    
    pthread_mutex_lock(&mysqla_lock);
    
    mysqla_task_t *ptr_taskIterator = first_async_task;
    while(ptr_taskIterator != NULL)
    {
        // Remember the next task in case we're about to delete the current task
        mysqla_task_t *ptr_nextTask = ptr_taskIterator->next;
        
        // Check if the task is done
        if(ptr_taskIterator->done)
        {
            // Result could be NULL due to MySQL error
            if(ptr_taskIterator->result != NULL)
            {
                // Pass the results to the GSC
                pushResultRows(ptr_taskIterator->result);
                
                // Free the MySQL result structure
                mysql_free_result(ptr_taskIterator->result);
                ptr_taskIterator->result = NULL;
            }
            else // No result, probably due to error
            {
                stackPushUndefined();
            }
            
            stackPushInt(ptr_taskIterator->taskId);
            
            // Call the callback. If the query was executed on a player, call it on a specific player
            bool startedThread = false;
            int threadId;
            if(ptr_taskIterator->entity != NULL)
            {
                // We don't want to call a callback on a disconnected player
                if(ptr_taskIterator->entityDisconnected == false)
                {
										printf("trying to call the callback on a player\n");
                    startedThread = true;
                    threadId = Scr_ExecEntThread(ptr_taskIterator->entity, (int)mysql_result_callback, 2);
                }
            }
            else
            {
								printf("trying to call the callback on the level\n");
                startedThread = true;
                threadId = Scr_ExecThread((int)mysql_result_callback, 2);
            }
            
            // Regardless of who it was called on, free the thread
            if(startedThread)
                Scr_FreeThread(threadId);
            
            // Remove the task from the linked list as it's finished now
            if(ptr_taskIterator->prev != NULL)
                ptr_taskIterator->prev->next = ptr_taskIterator->next;
            else
                first_async_task = ptr_taskIterator->next;
            
            if(ptr_taskIterator->next != NULL)
                ptr_taskIterator->next->prev = ptr_taskIterator->prev;
            
            // Free up the memory used by this task
            delete ptr_taskIterator;
        }
        
        // We may have deleted the current task
        ptr_taskIterator = ptr_nextTask;
    }
    
    pthread_mutex_unlock(&mysqla_lock);
}

/*
 * Log a MySQL error to the server's MySQL file (which gets created if it doesn't exist)
 */
static void log_mysql_error(const char *query, const int error, const char *strError)
{
    char filePathBuf[32] = {0};
    snprintf(filePathBuf, sizeof(filePathBuf), "../mysql_errors_%d.log", Shared_GetPort());
    
    pthread_mutex_lock(&mysqla_file_lock);
    
    FILE *mysqlFile = fopen(filePathBuf, "a");
    if(mysqlFile != NULL)
    {
        fprintf(mysqlFile, "Query \"%s\" caused error %d (%s)\n", query, error, strError);
        fclose(mysqlFile);
    }
    
    pthread_mutex_unlock(&mysqla_file_lock);
}

/*
 * Asynchronously execute a MySQL query with the specified connection handler
 */
static void *mysqla_execute_query(void *ptr_conn_arg)
{
    mysqla_connection_t *ptr_conn = (mysqla_connection_t *)ptr_conn_arg;
    printf("trying to execute query %s\n", ptr_conn->task->query);
    if(mysql_query(ptr_conn->connection, ptr_conn->task->query) == MYSQL_NO_ERROR)
    {
        // Only store the result if GSC wanted us to
        if(ptr_conn->task->save)
        {
            // Check if we can obtain a result from the query
            ptr_conn->task->result = mysql_store_result(ptr_conn->connection);
        }
        else
        {
            MYSQL_RES *result = mysql_store_result(ptr_conn->connection);
            if(result != NULL)
            {
                mysql_free_result(result);
            }
        }
    }
    else
    {
        const char *strError = mysql_error(ptr_conn->connection);
        const int error = mysql_errno(ptr_conn->connection);
        
        printf("ERROR: MySQL query (%s) failed with error %d (%s)\n", ptr_conn->task->query, error, strError);
        
        // Handle the file IO for appending to our mysql error log file
        log_mysql_error(ptr_conn->task->query, error, strError);
    }
    
    pthread_mutex_lock(&mysqla_lock);

    ptr_conn->task->done = true;
    ptr_conn->task = NULL;
    
    pthread_mutex_unlock(&mysqla_lock);
    
    return NULL;
}

/*
 * Asynchronous background MySQL handler.
 * Handles starting threads for each new MySQL query.
 */
static void *mysqla_query_handler(void *unused)
{
    // Only need to start the handler once. Starting it multiple times is a developer error.
    static bool started = false;
    if(started)
    {
        printf("ERROR: mysqla_query_handler() async handler already started.\n");
        return NULL;
    }
    
    started = true;
    
    // The first connection should be initialized before this function is called!
    mysqla_connection_t *ptr_conn = first_async_connection;
    if(ptr_conn == NULL)
    {
        printf("ERROR: mysqla_query_handler() async handler started before any connection was initialized\n");
        started = false;
        return NULL;
    }
    
    // Infinite loop, because this threaded function is the background handler
    mysqla_task_t *ptr_task;
    while(true)
    {
        // Lock access to MySQL global variables
        pthread_mutex_lock(&mysqla_lock);
        
        // Grab the first entries of the linked list
        ptr_task = first_async_task;
        ptr_conn = first_async_connection;
        while(ptr_task != NULL)
        {
            if(!ptr_task->started)
            {
                // Find an idle or unused connection 
                while(ptr_conn != NULL && ptr_conn->task != NULL)
                {
                    ptr_conn = ptr_conn->next;
                }
                
                // Looped through all connections are there are none available
                if(ptr_conn == NULL)
                    break;
                
                ptr_task->started = true;
                ptr_conn->task = ptr_task;
                
                // Create thread because we handle the query asynchronously.
                // Let threaded mysqla_execute_query do the work
                pthread_t query_thread;
                int error = pthread_create(&query_thread, NULL, mysqla_execute_query, ptr_conn);
                
                if(error)
                {
                    printf("ERROR: mysqla_query_handler() can't create thread (%i)\n", error);
                    break;
                }
                
                // Allow the system to immediately dispose the thread when it's done
                pthread_detach(query_thread);
                ptr_conn = ptr_conn->next;
            }
            
            // Loop through all tasks
            ptr_task = ptr_task->next;
        }
        pthread_mutex_unlock(&mysqla_lock);
        usleep(10000);
    }
    
    return NULL;
}

/*
 * Initialize a MySQL query (i.e. create a new task for it)
 */
static int mysqla_query_initializer(const char *sql, gentity_t *entity, bool save)
{
    // Each query has their own ID. It doesn't really matter if this overflows (it's a 32-bit integer)
    // This ID should not be randomized, as it increases the chances of a duplicate ID
    static int queryId = 0;
    
    pthread_mutex_lock(&mysqla_lock);
    
    queryId++;
    
    // Find the first task that is NULL so we can create a new task there
    mysqla_task_t *ptr_freeTask = first_async_task;
    mysqla_task_t *ptr_prevTask = NULL;
    while(ptr_freeTask != NULL)
    {
        ptr_prevTask = ptr_freeTask;
        ptr_freeTask = ptr_freeTask->next;
    }
    
    mysqla_task_t *ptr_taskNew = new mysqla_task_t;
    
    ptr_taskNew->taskId = queryId;
    snprintf(ptr_taskNew->query, sizeof(ptr_taskNew->query), "%s", sql);
    ptr_taskNew->prev = ptr_prevTask;
    ptr_taskNew->result = NULL;
    ptr_taskNew->save = save;
    ptr_taskNew->done = false;
    ptr_taskNew->next = NULL;
    ptr_taskNew->entity = entity;
    ptr_taskNew->entityDisconnected = false;
    ptr_taskNew->started = false;
    
    // If we've just set up the first task, reflect that into our global pointer
    if(ptr_prevTask == NULL)
    {
        first_async_task = ptr_taskNew;
    }
    else
    {
        ptr_taskNew->prev->next = ptr_taskNew;
    }
    
    pthread_mutex_unlock(&mysqla_lock);
    
    return queryId;
}

/************************************************************
 *              Functions callable from GSC                 *
 ************************************************************/
 
 /** Start of async MySQL functions **/

/*
 * Create a new query task on an entity
 * 
 * Arguments from GSC:
 *     char *query      - query string
 *     int saveResult   - whether or not to store the result
 * Returns to GSC:
 *     int id           - id of the newly created task
 */
void gsc_mysqla_create_entity_query(int num)
{
	printf("hey\n");
    char *query = NULL;
    int saveResult = 0; // By default we don't save the result
    
    // Obtain the arguments from GSC call
    printf("hello\n");
    stackGetParamString(0, &query);
    printf("world\n");
    stackGetParamInt(1, &saveResult);
    printf("foo\n");
    
    gentity_t *ptr_gentity = &g_entities[num];
    printf("bar\n");
    if(ptr_gentity == NULL) // Make sure the entity is known
    {
        printf("WARN: Calling query \"%s\" on non-existing entity\n", query);
        stackPushUndefined();
        return;
    }
    
    // Send back the ID of the newly created query task
    int id = mysqla_query_initializer(query, ptr_gentity, (saveResult > 0));
    stackPushInt(id);
}

/*
 * Create a new query task on the level
 * 
 * Arguments from GSC:
 *     char *query      - query string
 *     int saveResult   - whether or not to store the result
 * Returns to GSC:
 *     int id           - id of the newly created task
 */
void gsc_mysqla_create_level_query(void)
{
    char *query = NULL;
    int saveResult = 0; // By default we don't save the result
    
    // Obtain the arguments from GSC call
    stackGetParamString(0, &query);
    stackGetParamInt(1, &saveResult);
    
    // Send back the ID of the newly created query task
    int id = mysqla_query_initializer(query, NULL, (saveResult > 0));
    stackPushInt(id);
}

/*
 * Initialize all database connection structs and return their addresses
 * 
 * Arguments from GSC:
 *     char *host           - MySQL database server IP
 *     char *user           - MySQL database user name
 *     char *pass           - MySQL database user password
 *     char *db             - MySQL database name
 *     int port             - MySQL database server port
 *     int connectionCount  - Maximum amount of simultaneous connections to use
 * Returns to GSC:
 *     nothing
 */
void gsc_mysqla_initializer(void)
{
    // If the first entry of the linked list has already been set, then we've already initialized the database connections
    if(first_async_connection != NULL)
    {
        printf("ERROR: gsc_mysqla_initializer() async mysql already initialized\n");
        stackPushUndefined();
        return;
    }
    
    // Initialize the MySQL async lock
    if(pthread_mutex_init(&mysqla_lock, NULL) != 0)
    {
        printf("ERROR: Async mutex initialization failed\n");
        stackPushUndefined();
        return;
    }
    
    // Initialize the file IO lock
    if(pthread_mutex_init(&mysqla_file_lock, NULL) != 0)
    {
        printf("ERROR: File IO mutex initialization failed\n");
        stackPushUndefined();
        return;
    }
    
    // Obtain our GSC arguments
    int port, connection_count, callback;
    char *host, *user, *pass, *db;
    
    stackGetParamString(0, &host);
    stackGetParamString(1, &user);
    stackGetParamString(2, &pass);
    stackGetParamString(3, &db);
    stackGetParamInt(4, &port);
    stackGetParamInt(5, &connection_count);
    stackGetParamFunction(6, &callback);
    
    if(callback == -1)
    {
        stackError("ERROR: gsc_mysqla_initializer() needs a callback");
        stackPushUndefined();
        return;
    }
    
    mysql_result_callback = callback;
    
    // Check if the GSC developer is on drugs
    if(connection_count <= 0)
    {
        stackError("ERROR: gsc_mysqla_initializer() needs a positive connection_count in mysqla_initializer");
        stackPushUndefined();
        return;
    }
    
    pthread_mutex_lock(&mysqla_lock);

    for(int i = 0; i < connection_count; i++)
    {
        // Create and initialize the new connection struct
        mysqla_connection_t *ptr_newConnection = new mysqla_connection_t;
        ptr_newConnection->next = NULL;
        
        ptr_newConnection->connection = mysql_init(NULL);
        
        ptr_newConnection->connection = mysql_real_connect((MYSQL*)ptr_newConnection->connection, host, user, pass, db, port, NULL, 0);
        my_bool reconnect = true;
        mysql_options(ptr_newConnection->connection, MYSQL_OPT_RECONNECT, &reconnect);
        ptr_newConnection->task = NULL;
        
        // Add our newly created connection to the linked list
        ptr_newConnection->next = first_async_connection;
        first_async_connection = ptr_newConnection;
    }
    
    pthread_mutex_unlock(&mysqla_lock);
    
    pthread_t async_handler;
    if(pthread_create(&async_handler, NULL, mysqla_query_handler, NULL))
    {
        stackError("ERROR: gsc_mysqla_initializer() error detaching async handler thread");
        return;
    }
    
    pthread_detach(async_handler);
}

/*
 * This is called by the GSC when a player disconnects to make sure the task callbacks are no longer executed on this player
 */
void gsc_mysqla_ondisconnect(int num)
{
    gentity_t *ptr_gentity = &g_entities[num];
    
    if(ptr_gentity == NULL)
        return;
    
    pthread_mutex_lock(&mysqla_lock);
    
    mysqla_task_t *ptr_taskIterator = first_async_task;
    while(ptr_taskIterator != NULL)
    {
        // Check if the entity upon which this query was called just disconnected
        if(ptr_taskIterator->entity == ptr_gentity)
            ptr_taskIterator->entityDisconnected = true;
        
        ptr_taskIterator = ptr_taskIterator->next;
    }
    
    pthread_mutex_unlock(&mysqla_lock);
}


/** Start of synchronous MySQL functions **/

/*
 * Return the address of the existing synchronous connection, if available
 * 
 * Arguments from GSC:
 *     -
 * Returns to GSC:
 *     int connection (address of the actual MySQL connection) or undefined
 */
void gsc_mysqls_get_existing_connection(void)
{
    if(sync_mysql_connection == NULL)
    {
        stackPushUndefined();
    }
    else
    {
        stackPushInt((int)sync_mysql_connection);
    }
}

/*
 * Synchronously connect to a MySQL database 
 * 
 * Arguments from GSC:
 *     char *host   - MySQL database server IP
 *     char *user   - MySQL database user name
 *     char *pass   - MySQL database user password
 *     char *db     - MySQL database name
 *     int port     - MySQL database server port
 * Returns to GSC:
 *     -
 */
void gsc_mysqls_real_connect(void)
{
    if(sync_mysql_connection != NULL)
    {
        printf("ERROR: gsc_mysql_real_connect() already called\n");
        return;
    }
    
    MYSQL *mysql = mysql_init(NULL);
    sync_mysql_connection = mysql;
    
    int port;
    char *host, *user, *pass, *db;

    stackGetParamString(0, &host);
    stackGetParamString(1, &user);
    stackGetParamString(2, &pass);
    stackGetParamString(3, &db);
    stackGetParamInt(4, &port);

    int result = (int)mysql_real_connect(mysql, host, user, pass, db, port, NULL, 0);
    if(result != (int)mysql)
    {
        printf("ERROR: mysql_real_connect() failed with error %d\n", result);
        return;
    }
    
    my_bool reconnect = true;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
}

/*
 * Disconnect an existing synchronous MySQL database connection
 * 
 * Arguments from GSC:
 *     -
 * Returns to GSC:
 *     -
 */
void gsc_mysqls_close_connection(void)
{
    if(sync_mysql_connection == NULL)
    {
        printf("WARN: gsc_mysql_close_connection() no connection, can't close\n");
        return;
    }
    
    mysql_close((MYSQL *)sync_mysql_connection);
}

/*
 * Execute a synchronous MySQL query
 * 
 * Arguments from GSC:
 *     char *query    - Query string to execute
 *     int saveResult - Whether or not resulting rows should be returned
 * Returns to GSC:
 *     Resulting fields of all rows or undefined
 */
void gsc_mysqls_query(void)
{
    if(sync_mysql_connection == NULL)
    {
        printf("ERROR: gsc_mysql_query() no connection\n");
        stackPushUndefined();
        return;
    }
    
    char *query;
    stackGetParamString(0, &query);
    
    int saveResult = 0;
    stackGetParamInt(1, &saveResult);
    printf("Adding query %s, saving: %d\n", query, saveResult);
    // If the query resulted in an error, handle it and return
    int ret = mysql_query((MYSQL *)sync_mysql_connection, query);
    if(ret != 0)
    {
        const char *strError = mysql_error(sync_mysql_connection);
        const int error = mysql_errno(sync_mysql_connection);
        
        printf("ERROR: MySQL query (%s) failed with error %d (%s)\n", query, error, strError);
        
        // Handle the file IO for appending to our mysql error log file
        log_mysql_error(query, error, strError);
        stackPushUndefined();
        return;
    }
    
    // Check if we need to store the result and return the rows
    printf("query done\n");
    if(saveResult != 0)
    {
				printf("Saving result\n");
        MYSQL_RES *result = mysql_store_result(sync_mysql_connection);
        if(result != NULL)
        {
						printf("Result not NULL\n");
            // Pass the results to the GSC
            pushResultRows(result);

            // Free the MySQL result structure
            mysql_free_result(result);
            
            return;
        }
    }
    
    // Always push undefined if we have not pushed anything else
    stackPushUndefined();
}

/*
 * Obtain error code of MySQL query of current connection
 * 
 * Arguments from GSC:
 *     - 
 * Returns to GSC:
 *     int ret        - Error code of last MySQL query of the current connection (or undefined if no connection)
 */
void gsc_mysqls_errno(void)
{
    if(sync_mysql_connection == NULL)
    {
        printf("ERROR: gsc_mysql_errno() no connection\n");
        stackPushUndefined();
        return;
    }
    
    int ret = mysql_errno((MYSQL *)sync_mysql_connection);
    stackPushInt(ret);
}

/*
 * Obtain error string of MySQL query of current connection
 * 
 * Arguments from GSC:
 *     - 
 * Returns to GSC:
 *     char *ret        - Error string of last MySQL query of the current connection (or undefined if no connection)
 */
void gsc_mysqls_error(void)
{
    if(sync_mysql_connection == NULL)
    {
        printf("ERROR: gsc_mysql_error() no connection\n");
        stackPushUndefined();
        return;
    }

    char *ret = (char *)mysql_error((MYSQL *)sync_mysql_connection);
    stackPushString(ret);
}

/*
 * Obtain number of affected rows for most recent query
 * 
 * Arguments from GSC:
 *     - 
 * Returns to GSC:
 *     int ret        - Amount of affected rows (or undefined if no connection)
 */
void gsc_mysqls_affected_rows(void)
{
    if(sync_mysql_connection == NULL)
    {
        printf("ERROR: gsc_mysql_affected_rows() no connection\n");
        stackPushUndefined();
        return;
    }
    
    int ret = mysql_affected_rows((MYSQL *)sync_mysql_connection);
    stackPushInt(ret);
}

/*
 * Obtain number of rows from a MySQL result
 * 
 * Arguments from GSC:
 *     int result     - Address of the MySQL result structure
 * Returns to GSC:
 *     int ret        - Number of rows in the result
 */
void gsc_mysqls_num_rows(void)
{
    int result;
    stackGetParamInt(0, &result);

    int ret = mysql_num_rows((MYSQL_RES *)result);
    stackPushInt(ret);
}

/*
 * Obtain number of fields from a MySQL result
 * 
 * Arguments from GSC:
 *     int result     - Address of the MySQL result structure
 * Returns to GSC:
 *     int ret        - Number of fields in the result
 */
void gsc_mysqls_num_fields(void)
{
    int result;
    stackGetParamInt(0, &result);

    int ret = mysql_num_fields((MYSQL_RES *)result);
    stackPushInt(ret);
}

/*
 * Seek to a specified offset in the MySQL result structure
 * 
 * Arguments from GSC:
 *     int result     - Address of the MySQL result structure
 *     int offset     - Offset in the result structure
 * Returns to GSC:
 *     int ret        - True if successful otherwise False
 */
void gsc_mysqls_field_seek(void)
{
    int result;
    int offset;
    stackGetParamInt(0, &result);
    stackGetParamInt(1, &offset);

    int ret = mysql_field_seek((MYSQL_RES *)result, offset);
    stackPushInt(ret);
}

/*
 * Fetch a field in the MySQL result structure
 * 
 * Arguments from GSC:
 *     int result     - Address of the MySQL result structure
 * Returns to GSC:
 *     char *ret      - The name of the field (or undefined)
 */
void gsc_mysqls_fetch_field(void)
{
    int result;
    stackGetParamInt(0, &result);

    MYSQL_FIELD *field = mysql_fetch_field((MYSQL_RES *)result);
    {
        stackPushUndefined();
        return;
    }
    
    char *ret = field->name;
    stackPushString(ret);
}

/*
 * Sanitize an input string
 * 
 * Arguments from GSC:
 *     int result     - Address of the MySQL result structure
 *     char *str      - Input string to sanitize
 * Returns to GSC:
 *     - char *ret    - Sanitized input string
 */
void gsc_mysqls_real_escape_string(void)
{
    
    char *str;
    stackGetParamString(0, &str);

    // It is possible that every character is escaped, so multiply by 2 (and allocate for NULL terminator)
    char *ret = (char *)malloc(strlen(str) * 2 + 1);
    
    mysql_real_escape_string(sync_mysql_connection, ret, str, strlen(str));
    
    stackPushString(ret);
    
    free(ret);
}