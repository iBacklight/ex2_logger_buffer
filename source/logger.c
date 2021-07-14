/*
 * Copyright (C) 2015  Brendan Bruner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * bbruner@ualberta.ca
 */
/**
 * @file logger.c
 * @author Brendan Bruner, Haoran Qi
 * @date May 15, 2015
 *
 */

#include <string.h> 
#include <stdio.h>
#include <stdbool.h>
#include <logger.h>
#include "util/service_utilities.h"

/********************************************************************************/
/* Defines																		*/
/********************************************************************************/
#define LOGGER_META_HEAD_START 0
#define	LOGGER_META_TAIL_START (FILESYSTEM_MAX_NAME_LENGTH+1)
#define LOGGER_CONTROL_DATA_LENGTH ((2*(FILESYSTEM_MAX_NAME_LENGTH+1))+3+4+7+2)
#define LOGGER_META_TEM_START ((2*(FILESYSTEM_MAX_NAME_LENGTH+1))+3+4)
#define LOGGER_META_TEM_LENGTH 7

/*Some pending defines regarding io func*/
#define FILE_WRITE_ERR 0
#define FILE_READ_ERR 0
#define FILE_SEEK_SUCCESS 0
#define GET_NULL_FILE NULL
#define RED_FILE_ERR -1

/********************************************************************************/
/* Singletons																	*/
/********************************************************************************/
/* All logger instance share the same mutex. */
static SemaphoreHandle_t logger_sync_mutex;

/********************************************************************************/
/* Private Method Definitions													*/
/********************************************************************************/
static inline void lock_mutex(SemaphoreHandle_t mutex){
	DEV_ASSERT(mutex);
    xSemaphoreTake(mutex, portMAX_DELAY);	
}

static inline void unlock_mutex(SemaphoreHandle_t mutex){
	DEV_ASSERT(mutex);
    xSemaphoreGive(mutex);
}

// ssize_t fsize(char const* filename){
// 	struct stat st;
// 	if(stat(filename, &st) == 0){
// 		return st.st_size;
// 	}
// 	/*File read failed, return a negative number*/
// 	return -1;
// }

static unsigned int logger_powerof( unsigned int base, unsigned int exp )
{
  unsigned int i;
  unsigned int num = 1;
  for( i = 0; i < exp; ++i ) {
    num = num * base;
  }
  return num;
}

static void logger_uitoa( unsigned int num, char* str, size_t len, int base )
{
  size_t i;
  unsigned int rem;
  char ascii_num;

  /* Zero fill string. */
  for( i = 0; i < len; ++i ) {
    str[i] = '0';
  }

  /* Check for zero. */
  if( num == 0 ) {
    return;
  }

  for( i = 0; i < len; ++i ) {
    rem = num % base;
    ascii_num = (rem > 9) ? ((rem-10) + 'a') : (rem + '0');
    str[len-i-1] = ascii_num;
    num = (num - rem) / base;
  }
}

void ocp_uito( unsigned int num, char* str, size_t len, int base )
{
	logger_uitoa(num, str, len, base);
}

static unsigned int logger_atoui( const char* str, size_t len, int base )
{
  size_t i;
  unsigned int num = 0;
  unsigned int inter_num = 0;


  for( i = 0; i < len; ++i ) {
    /* Convert ascii byte to int. */
    if( str[i] >= '0' && str[i] <= '9' ) {
      inter_num = str[i] - '0';
    }
    else if( str[i] >= 'a' && str[i] <= 'z' ) {
      inter_num = str[i] - 'a' + 10;
    }
    else if( str[i] >= 'A' && str[i] <= 'Z' ) {
      inter_num = str[i] - 'A' + 10;
    }
    else {
      return 0;
    }

    /* accumulate. */
    num += inter_num * logger_powerof(base, len - i - 1);
  }

  return num;
}

unsigned int ocp_atoui( const char* str, size_t len, int base )
{
	return logger_atoui(str, len, base);
}

/**
 * @memberof logger_t @private
 * @brief
 * 		Get the next file name.
 * @details
 * 		Given the current file name, <b>name</b>, compute the next name of
 * 		a file. <br>For example, the next name, with a max capacity of 40, of 039G2304
 * 		is 000G2305.
 * 		<br>With a max capacity of 120, the next name would of 103Y0032 would be
 * 		104Y0033.
 * @param current_name[in/out]
 * 		When this function is called, this is the current name of the file
 * 		pointed to by HEAD, when the function returns, this will now be the
 * 		name of the new file to be the new HEAD.
 */
static void logger_next_name( logger_t* self, char* name )
{

	DEV_ASSERT(self);
	DEV_ASSERT(name);

	unsigned int next_seq = 0;
	unsigned int next_tem = 0;

	/* Increment temporal and sequence number, then roll over if necessary.
	 * We can't use MOD for next_seq because logger_t::max_capacity can change dynamically. If it were
	 * to suddenly decrease and next_seq became greater than  logger_t::max_capacity, the MOD would not
	 * return the desired value.
	 */
	next_seq = (logger_atoui(name + LOGGER_SEQUENCE_START, LOGGER_TOTAL_SEQUENCE_BYTES, LOGGER_SEQUENCE_BASE) + 1);
	if( next_seq >= self->max_capacity ) {
		next_seq = 0;
	}
	next_tem = (((name[4]-'0')*1000) + ((name[5]-'0')*100) + ((name[6]-'0')*10) + (name[7]-'0') + 1) % LOGGER_MAX_TEMPORAL_POINTS;

	/* Convert numbers back to ascii and put in name string. */
	logger_uitoa(next_seq, name + LOGGER_SEQUENCE_START, LOGGER_TOTAL_SEQUENCE_BYTES, LOGGER_SEQUENCE_BASE);
//	name[2] = (char) ((next_seq % 20) + 'a') & 0xFF;
//	next_seq = (next_seq - (next_seq % 20)) / 20; // basically a base ten logical shift right.
//	name[1] = (char) ((next_seq % 20) + 'a') & 0xFF;
//	next_seq = (next_seq - (next_seq % 10)) / 10; // basically a base ten logical shift right.
//	name[0] = (char) ((next_seq % 20) + 'a') & 0xFF;

	name[7] = (char) ((next_tem % 10) + '0') & 0xFF;
	next_tem = (next_tem - (next_tem % 10)) / 10; // basically a base ten logical shift right.
	name[6] = (char) ((next_tem % 10) + '0') & 0xFF;
	next_tem = (next_tem - (next_tem % 10)) / 10; // basically a base ten logical shift right.
	name[5] = (char) ((next_tem % 10) + '0') & 0xFF;
	next_tem = (next_tem - (next_tem % 10)) / 10; // basically a base ten logical shift right.
	name[4] = (char) ((next_tem % 10) + '0') & 0xFF;

	//snprintf(name, LOGGER_TOTAL_SEQUENCE_BYTES, "%d", next_seq);
	//snprintf(name+LOGGER_TEMPORAL_START, LOGGER_TOTAL_TEMPORAL_BYTES, "%d", next_tem);
}

/**
 * @memberof logger_t
 * @private
 * @brief
 * 		Creates a new control data file.
 * @details
 * 		Creates a new control data file. All data is wiped from the existing file (if one
 * 		exists).
 * 		| HEAD (LOGGER_MAX_FILE_NAME_LENGTH+1 bytes) | TAIL (LOGGER_MAX_FILE_NAME_LENGTH+1 bytes) |
 * 		| HEAD sequence data (3 bytes) | HEAD temporal data (4 bytes) | popped temporal data (7 bytes) | reserved (2 bytes) |
 */
static logger_error_t logger_create_control_file( logger_t* self )
{
	DEV_ASSERT(self);

	int32_t 	control_file_handle;
	char 	control_string[LOGGER_CONTROL_DATA_LENGTH];
	int32_t     bytes_write;

	/* Write to a variable the initial set of control data. */
	snprintf(control_string, LOGGER_CONTROL_DATA_LENGTH, "000%c0000.log%c000%c0000.log%c0000000000000000", self->element_file_name, '\0', self->element_file_name, '\0');
	printf("%s",control_string);

	/* Open control file. */
	control_file_handle = red_open(self->control_file_name, RED_O_WRONLY);
	if( RED_FILE_ERR == control_file_handle) {
		/* File system failure. */
		//exit(red_errno);
		return LOGGER_NVMEM_ERR;
	}

	/* Write control data into control file. */
	bytes_write = red_write(control_file_handle, control_string, LOGGER_CONTROL_DATA_LENGTH);
	red_close(control_file_handle);
	if( bytes_write == RED_FILE_ERR ) {
		/* File system failure. */
		return LOGGER_NVMEM_ERR;
	} else if( bytes_write != LOGGER_CONTROL_DATA_LENGTH ) {
		/* Out of memory :( */
		printf("Create control file failed, read_bytes: %d\n", bytes_write);
		return LOGGER_NVMEM_FULL;
	}
	return LOGGER_OK;
}

/**
 * @memberof logger_t
 * @private
 * @brief
 * 		Cache control.
 * @details
 * 		Caches control data from the control data file. If no control data file exists, it creates one.
 */
static logger_error_t logger_cache_control_data( logger_t* self )
{
	DEV_ASSERT(self);

	int32_t		control_file_handle;
	int32_t	bytes_read;
	logger_error_t lerr;


	control_file_handle = red_open(self->control_file_name, RED_O_RDONLY);
	if( RED_FILE_ERR == control_file_handle ) {
		/* Need to create the control file, it doesn't exist. */
		lerr = logger_create_control_file(self);
		if( lerr == LOGGER_OK ) {
			control_file_handle = red_open(self->control_file_name, RED_O_RDONLY);
			/* Make sure control file created successfully. */
			if ( RED_FILE_ERR == control_file_handle) {
				/* Error, return. */
				return LOGGER_NVMEM_ERR;
			}
		}else {
			return lerr;
		}
	}
	/* Cache HEAD. */
	bytes_read = red_read(control_file_handle, self->head_file_name,  FILESYSTEM_MAX_NAME_LENGTH+1);
	if(  RED_FILE_ERR == bytes_read ) {
		/* Failed to read head into memory. */
		red_close(control_file_handle);
		return LOGGER_NVMEM_ERR;
	} else if( bytes_read != (FILESYSTEM_MAX_NAME_LENGTH+1) ) {
		/* File is too small, wipe it and create new one. */
		return logger_create_control_file(self);
	}
	self->head_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0';

	/* Cache TAIL. */
	bytes_read = red_read(control_file_handle, self->tail_file_name,  FILESYSTEM_MAX_NAME_LENGTH+1);
	red_close(control_file_handle);
	if( RED_FILE_ERR == bytes_read ) {
		/* Failed to read head into memory. */
		return LOGGER_NVMEM_ERR;
	} else if( bytes_read != (FILESYSTEM_MAX_NAME_LENGTH+1) ) {
		/* File is too small, wipe it and create new one. */
		return logger_create_control_file(self);
	}
	self->tail_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0';
	return LOGGER_OK;
}

/**
 * @memberof logger_t @private
 * @brief
 * 		Get the file name of the HEAD.
 * @details
 * 		Get the file name of the HEAD. This method just calls
 * 		logger_cache_control_data then returns logger_t::head_file_name.
 */
static char* logger_get_head( logger_t* self, logger_error_t* err )
{
	DEV_ASSERT(self);
	DEV_ASSERT(err);

	*err = logger_cache_control_data(self);
	return self->head_file_name;
}

/**
 * @memberof logger_t @private
 * @brief
 * 		Set the file name of the HEAD.
 * @details
 * 		Set the file name of the HEAD.
 * @param head [in]
 * 		Must point to a string of at least FILESYSTEM_MAX_NAME_LENGTH bytes.
 */
static logger_error_t logger_set_head( logger_t* self, char const* head )
{
	DEV_ASSERT(self);
	DEV_ASSERT(head);

	int32_t		control_file_handle;
	int32_t	bytes_written;

	control_file_handle = red_open(self->control_file_name, RED_O_WRONLY);
	if( RED_FILE_ERR == control_file_handle) {
		/* File system failure. */
		return LOGGER_NVMEM_ERR;
	}

	bytes_written = red_write(control_file_handle, head, FILESYSTEM_MAX_NAME_LENGTH);
	red_close(control_file_handle);
	if(  RED_FILE_ERR == bytes_written ) {
		return LOGGER_NVMEM_ERR;
	}
	if( bytes_written < FILESYSTEM_MAX_NAME_LENGTH ) {
		return LOGGER_NVMEM_FULL;
	}
	return LOGGER_OK;
}

/**
 * @memberof logger_t @private
 * @brief
 * 		Get the file name of the TAIL.
 * @details
 * 		Get the file name of the TAIL. This method just calls
 * 		logger_cache_control_data then returns logger_t::tail_file_name.
 */
static char* logger_get_tail( logger_t* self, logger_error_t* err )
{
	DEV_ASSERT(self);
	DEV_ASSERT(err);

	*err = logger_cache_control_data(self);
	return self->tail_file_name;
}

/**
 * @memberof logger_t @private
 * @brief
 * 		Set the file name of the TAIL.
 * @details
 * 		Set the file name of the TAIL.
 */
static logger_error_t logger_set_tail( logger_t* self, char const* tail )
{
	DEV_ASSERT(self);
	DEV_ASSERT(tail);

	int32_t		control_file_handle;
	int32_t	bytes_written, ferr;

	control_file_handle = red_open(self->control_file_name, RED_O_WRONLY);
	if( RED_FILE_ERR == control_file_handle) {
		/* File system failure. */
		return LOGGER_NVMEM_ERR;
	}

	ferr = red_lseek(control_file_handle, LOGGER_META_TAIL_START, RED_SEEK_SET);
	if( RED_FILE_ERR == ferr ) {
		red_close(control_file_handle);
		return LOGGER_NVMEM_ERR;
	}

	bytes_written = red_write(control_file_handle, tail, FILESYSTEM_MAX_NAME_LENGTH);
	red_close(control_file_handle);
	if( bytes_written == RED_FILE_ERR ) {
		return LOGGER_NVMEM_ERR;
	}
	if( bytes_written < FILESYSTEM_MAX_NAME_LENGTH ) {
		return LOGGER_NVMEM_FULL;
	}
	return LOGGER_OK;
}

/**
 * @memberof logger_t
 * @private
 * @brief
 * 		Rename a file to remove it from the ring buffer's tracking.
 * @details
 * 		Rename a file to remove it from the ring buffer's tracking.
 * 		This will give the file the naming convention:
 * 		<br><b>Xaaaaaaa.bin</b>
 * 		<br>As defined in by logger_t documentation.
 */
static logger_error_t logger_untrack_file( logger_t* self, char* file_name )
{
	DEV_ASSERT(self);
	DEV_ASSERT(file_name);

	char 		new_name[FILESYSTEM_MAX_NAME_LENGTH+1];
	int32_t		control_file_handle;	
	int			temporal_point;
	int32_t	bytes_read, ferr;

	/* Open control file. */
	control_file_handle = red_open(self->control_file_name, RED_O_RDWR);
	if( RED_FILE_ERR == control_file_handle) {
		/* File system failure. */
		return LOGGER_NVMEM_ERR;
	}
	/* Seek to temporal data. */
	ferr = red_lseek(control_file_handle, LOGGER_META_TEM_START, RED_SEEK_SET);
	if( RED_FILE_ERR == ferr ) {
		red_close(control_file_handle);
		return LOGGER_NVMEM_ERR;
	}
	
	/* Get temporal point. */
	bytes_read = red_read(control_file_handle, new_name+1, LOGGER_META_TEM_LENGTH);
	if( bytes_read == RED_FILE_ERR || bytes_read != LOGGER_META_TEM_LENGTH ) {
		red_close(control_file_handle);
		return LOGGER_NVMEM_ERR;
	}

	/* Finish off the name. */
	new_name[0] = self->element_file_name;
	new_name[8] = '.';
	new_name[9] = 'b';
	new_name[10] = 'i';
	new_name[11] = 'n';
	new_name[12] = '\0';

	/* Get next temporal point. */
	temporal_point = (((new_name[0]-'0')*1000000) + ((new_name[1]-'0')*100000) + ((new_name[2]-'0')*10000) + ((new_name[3]-'0')*1000) + ((new_name[4]-'0')*100) + ((new_name[5]-'0')*10) + ((new_name[6]-'0')*1) + 1) % (10*10*10*10*10*10*10);

	/* Write the new point back to string. */
	new_name[7] = (char) ((temporal_point % 10) + '0') & 0xFF;
	temporal_point = (temporal_point - (temporal_point% 10)) / 10; // basically a base ten logical shift right.
	new_name[6] = (char) ((temporal_point % 10) + '0') & 0xFF;
	temporal_point = (temporal_point - (temporal_point% 10)) / 10; // basically a base ten logical shift right.
	new_name[5] = (char) ((temporal_point % 10) + '0') & 0xFF;
	temporal_point = (temporal_point - (temporal_point% 10)) / 10; // basically a base ten logical shift right.
	new_name[4] = (char) ((temporal_point % 10) + '0') & 0xFF;
	temporal_point = (temporal_point - (temporal_point% 10)) / 10; // basically a base ten logical shift right.
	new_name[3] = (char) ((temporal_point % 10) + '0') & 0xFF;
	temporal_point = (temporal_point - (temporal_point% 10)) / 10; // basically a base ten logical shift right.
	new_name[2] = (char) ((temporal_point % 10) + '0') & 0xFF;
	temporal_point = (temporal_point - (temporal_point% 10)) / 10; // basically a base ten logical shift right.
	new_name[1] = (char) ((temporal_point % 10) + '0') & 0xFF;

	/* Update new temporal point in file. */
	/* Seek to temporal data. */
	ferr = red_lseek(control_file_handle, LOGGER_META_TEM_START, RED_SEEK_SET);
	if( RED_FILE_ERR == ferr ) {
		red_close(control_file_handle);
		return LOGGER_NVMEM_ERR;
	}
	
	/* Set temporal point. */
	bytes_read = red_write(control_file_handle, (new_name+1), LOGGER_META_TEM_LENGTH);
	red_close(control_file_handle);
	if( bytes_read == RED_FILE_ERR || bytes_read != LOGGER_META_TEM_LENGTH ) {
		return LOGGER_NVMEM_ERR;
	}

	/* Rename the file, first, check if a file with this name already exist. If it does, delete it. */
	control_file_handle = red_open(new_name, RED_O_RDWR);
	if( control_file_handle != RED_FILE_ERR ) {
		red_close(control_file_handle);
		ferr = red_rmdir(new_name);
		if(RED_FILE_ERR == ferr){
			return LOGGER_NVMEM_ERR;
		}
	}
	ferr = red_rename(file_name, new_name);
	if( RED_FILE_ERR == ferr ) {
		return LOGGER_NVMEM_ERR;
	}

	strncpy(file_name, new_name, FILESYSTEM_MAX_NAME_LENGTH);
	file_name[FILESYSTEM_MAX_NAME_LENGTH] = "\0";
	return LOGGER_OK;
}

/**
 * @memberof logger_t
 * @private
 * @brief
 * 		Update tail position.
 * @details
 * 		Updates the position of the tail in the ring buffer. This is useful when asynchronous
 * 		file removals have rendered the tail position corrupt (ie, pointing to a non existant file).
 * @returns
 * 		Error code
 */
static logger_error_t logger_update_tail( logger_t* self )
{
	DEV_ASSERT(self);

	int32_t 			fp;
	char*			tail_file_name;
	char*			head_file_name;
	logger_error_t	lerr;
	unsigned int	i;
	//fs_error_t		ferr;
	bool_t			do_update = false;

	tail_file_name = logger_get_tail(self, &lerr);
	if( lerr != LOGGER_OK ) {
		return lerr;
	}

	head_file_name = logger_get_head(self, &lerr);
	if( lerr != LOGGER_OK ) {
		return lerr;
	}

	/* From the current tail, there is a maximum of logger_t::max_capacity elements to search. */
	for( i = 0; i < self->max_capacity; ++i ) {
		/* Check if this tail file exists (ie, check if it has been asynchronously removed. */
		fp = red_open(tail_file_name, RED_O_RDONLY);
		if( RED_FILE_ERR == fp ) {
			/* The file doesn't exist. See if this is also the HEAD file. */
			/* If HEAD == TAIL and this file doesn't exist then the buffer has */
			/* been asynchronously emptied (all files deleted). */
			if( 0 == strncmp(tail_file_name, head_file_name, FILESYSTEM_MAX_NAME_LENGTH) ) {
				return LOGGER_EMPTY;
			}

			/* No file, and HEAD != TAIL, keep searching for the current TAIL. */
			logger_next_name(self, tail_file_name);
		} else {
			/* Element has a file. */
			do_update = true;
			break;
		} 
		// else {
		// 	/* Some file system error. */
		// 	return LOGGER_NVMEM_ERR;
		// }
	}

	if( do_update ) {
		/* Update the tail meta data. */
		return logger_set_tail(self, tail_file_name);
	}
	return LOGGER_OK;
}

/* *****************************
   Construct & Deconstruct func
   ***************************** */
static void destroy( logger_t *self )
{
	DEV_ASSERT( self );
}

logger_error_t initialize_logger
(
	logger_t *self,
	//FILE *filesystem,
	char const *control_file_name,
	char element_file_name,
	size_t max_capacity,
	bool_t logger_is_init
)
{
	
	DEV_ASSERT( self );
	//DEV_ASSERT( filesystem );
	DEV_ASSERT( control_file_name );
	/* Link virtual methods. */
	self->destroy = destroy;

	/* Initialize singleton mutex. */
	if( logger_is_init == MUTEX_FALSE ) {
		logger_sync_mutex = xSemaphoreCreateMutex();
		if( logger_sync_mutex == NULL ) {
			return LOGGER_MUTEX_ERR;
		}
		logger_is_init = MUTEX_TURE;
		/* Delete */
		xSemaphoreGive( logger_sync_mutex );
	}

	/* Setup Member data. */
	//self->fs = filesystem;
	self->sync_mutex = &logger_sync_mutex;
	self->element_file_name = element_file_name;
	if( max_capacity > LOGGER_MAX_CAPACITY || max_capacity < LOGGER_MIN_CAPCITY ) {
		return LOGGER_INV_CAP;
	}
	self->max_capacity = max_capacity;
	self->head_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '00010000.log';
	self->tail_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '00010000.log';

    /*strncpy( self->head_file_name, '00010000.log', FILESYSTEM_MAX_NAME_LENGTH );
    self->head_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0';
    strncpy( self->tail_file_name, '00010000.log', FILESYSTEM_MAX_NAME_LENGTH );
    self->tail_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0';*/
	/* Copy control file name into logger instance. */
	strncpy( self->control_file_name, control_file_name, FILESYSTEM_MAX_NAME_LENGTH );
	self->control_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0'; /* Fail safe. */

	/* Cache control data within the control data file. */
	return logger_cache_control_data(self);
}



/********************************************************************************/
/* Public Method Definitions													*/
/********************************************************************************/
int32_t logger_peek_head( logger_t* self, logger_error_t* err )
{
	DEV_ASSERT( self );
	DEV_ASSERT( err );

	int32_t			head_file_handle;
	int64_t		file_err;
	logger_error_t	logger_err;
	char const*		head_file_name;
	//ssize_t			eof;
	REDSTAT    		*pStat;

	lock_mutex( *self->sync_mutex);

	/* Get name of file at HEAD. */
	head_file_name = logger_get_head(self, &logger_err);
	if( logger_err != LOGGER_OK ) {
		unlock_mutex( *self->sync_mutex );
		return GET_NULL_FILE;
	}

	/* Open the file. */
	head_file_handle = red_open(head_file_name, RED_O_RDONLY);
	unlock_mutex( *self->sync_mutex );
	if( RED_FILE_ERR == head_file_handle ) {
		*err = LOGGER_EMPTY;
		return GET_NULL_FILE;
	}

	/* Get size of the file so we can seek to the end of it. */
	if(red_fstat(head_file_handle, pStat) != 0){
		*err = LOGGER_NVMEM_ERR;
		red_close(head_file_handle);
		return GET_NULL_FILE;
	}

	/* Seek to the end of the file. */
	file_err = red_lseek(head_file_handle, pStat->st_size, RED_SEEK_SET);
	if( RED_FILE_ERR == file_err ) {
		*err = LOGGER_NVMEM_ERR;
		red_close(head_file_handle);
		return GET_NULL_FILE;
	}
	*err = LOGGER_OK;
	return head_file_handle;
}

/* Insert a given filename as head name 
   NOte: Only rename new file name to current head name*/
int32_t logger_insert( logger_t* self, logger_error_t* err, char const* file_to_insert_name )
{
	DEV_ASSERT(self);
	DEV_ASSERT(err);

	logger_error_t 	lerr;
	uint32_t		fs_err;
	char* 			head_file_name;
	char*			tail_file_name;
	char			new_head_file_name[FILESYSTEM_MAX_NAME_LENGTH+1];
	int32_t			head_file_handle;

	lock_mutex(*self->sync_mutex);
	/* Get the name (position) of the HEAD and TAIL. */
	head_file_name = logger_get_head(self, &lerr);
	if( lerr != LOGGER_OK ) {
		unlock_mutex(*self->sync_mutex);
		*err = lerr;
		return GET_NULL_FILE;
	}
	if( (head_file_handle = red_open(head_file_name, RED_O_RDONLY)) == RED_FILE_ERR ) {
		/* The HEAD file doesn't exist => logger emptied via asynchronous file removal. */
		lerr = logger_create_control_file(self); /* FIXME: only reset head/tail pointers - not temporal data too */
		if( lerr != LOGGER_OK ) {
			unlock_mutex(*self->sync_mutex);
			*err = lerr;
			return GET_NULL_FILE;
		}
		head_file_name = logger_get_head(self, &lerr);
		if( lerr != LOGGER_OK ) {
			unlock_mutex(*self->sync_mutex);
			*err = lerr;
			return GET_NULL_FILE;
		}
	}else{red_close(head_file_handle);}

	tail_file_name = logger_get_tail(self, &lerr);
	if( lerr != LOGGER_OK ) {
		unlock_mutex(*self->sync_mutex);
		*err = lerr;
		return GET_NULL_FILE;
	}
	/* Increment HEAD to next element. */
	logger_next_name(self, head_file_name);

	/* Check if HEAD == TAIL. To do this, we only need to look at the sequencing bytes (first three bytes). */
	if( strncmp(tail_file_name, head_file_name, LOGGER_TOTAL_SEQUENCE_BYTES) == 0 ) {
		/* HEAD and TAIL are overlapping. */
		/* Since asynchronous file removals can happen, lets first update position of the TAIL and then see if */
		/* the HEAD and TAIL still overlap. */
		lerr = logger_update_tail(self);
		if( lerr != LOGGER_OK ) {
			unlock_mutex(*self->sync_mutex);
			*err = lerr;
			return GET_NULL_FILE;
		}

		/* Get new TAIL. */
		tail_file_name = logger_get_tail(self, &lerr);
		if( lerr != LOGGER_OK ) {
			unlock_mutex(*self->sync_mutex);
			*err = lerr;
			return GET_NULL_FILE;
		}
		/* Since get tail method refreshes cache - the cached head name is also refreshed so must recall next name method. */
		logger_next_name(self, head_file_name);

		if( strncmp(tail_file_name, head_file_name, LOGGER_TOTAL_SEQUENCE_BYTES) == 0 ) {
			/* HEAD and TAIL still overlap. Remove the TAIL so it can be replaced. */
			if (red_rmdir(tail_file_name) == 0){
				logger_next_name(self, tail_file_name);
				lerr = logger_set_tail(self, tail_file_name);
				
				if( lerr != LOGGER_OK ) {
					/* Failed to increment TAIL. */
					unlock_mutex(*self->sync_mutex);
					*err = lerr;
					return GET_NULL_FILE;
				}
			}else{
				return GET_NULL_FILE;
			}
		}
	}
	
	/* Insert at HEAD. */
	/* First check if we are inserting an empty file. */
	if( file_to_insert_name == NULL ) {
		/* Inserting an empty file, lets create it. */
		head_file_handle = red_open(head_file_name, RED_O_RDWR);
		if(RED_FILE_ERR == head_file_handle){return GET_NULL_FILE;}
		
	} else {
		/* Inserting the file given as a function argument. Lets process that string to avoid some errors. */
		strncpy(new_head_file_name, file_to_insert_name, FILESYSTEM_MAX_NAME_LENGTH);
		new_head_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0';
		/* Now rename it so that the ring buffer can track it. */
		/* Due to corruption, a file by this name may exist already, remove it if one does. */
		//origin: head_file_name, which is strange: why delete it and rename it again?
		if( (head_file_handle = red_open(head_file_name, RED_O_RDWR) )!= RED_FILE_ERR ) {
			/*close the file before removal*/
			red_close(head_file_handle);
			red_rmdir(head_file_name);
		}
		fs_err = red_rename(new_head_file_name, head_file_name);
		if( fs_err != 0 ) {
			/* Failed to rename it, all we can do is abort. */
			unlock_mutex(*self->sync_mutex);
			*err = LOGGER_NVMEM_ERR;
			return GET_NULL_FILE;
		}
		/* Finally, lets open it. */
		head_file_handle = red_open(head_file_name, RED_O_RDWR);
	}

	/* Check we opened the file without errors. */
	if( RED_FILE_ERR == head_file_handle ) {
		/* Failed to open the file, all we can do is abort. */
		unlock_mutex(*self->sync_mutex);
		*err = LOGGER_NVMEM_ERR;
		return GET_NULL_FILE;
	}
	/* The file is open and named such that it can be the HEAD, so, lets make it so. */
	lerr = logger_set_head(self, head_file_name);
	if( lerr != LOGGER_OK ) {
		/* Failed to set HEAD. */
		red_close(head_file_handle);
		unlock_mutex(*self->sync_mutex);
		*err = lerr;
		return GET_NULL_FILE;
	}
	/* Insert successful.. */
	unlock_mutex( *self->sync_mutex );
	red_close(head_file_handle);
	*err = LOGGER_OK;
	return head_file_handle;
}

/**/
int32_t logger_peek_tail( logger_t* self, logger_error_t* err )
{
	DEV_ASSERT( self );
	DEV_ASSERT( err );

	int32_t 	tail_file_handle;
	char const* tail_file_name;

	lock_mutex( *self->sync_mutex);

	/* Get name of tail. */
	tail_file_name = logger_get_tail(self, err);
	if( *err != LOGGER_OK ) {
		unlock_mutex(*self->sync_mutex);
		return GET_NULL_FILE;
	}

	/* Open tail file. */
	tail_file_handle = red_open(tail_file_name, RED_O_RDWR);
	if( RED_FILE_ERR == tail_file_handle ) {
		/* Tail needs to be updated. */
		*err = logger_update_tail(self);
		if( *err != LOGGER_OK ) {
			unlock_mutex(*self->sync_mutex);
			return GET_NULL_FILE;
		}

		/* Updated tail, try opening the file again. */
		tail_file_name = logger_get_tail(self, err);
		if( *err != LOGGER_OK ) {
			unlock_mutex(*self->sync_mutex);
			return GET_NULL_FILE;
		}
		tail_file_handle = red_open(tail_file_name, RED_O_RDONLY);
	}
	/*Check if the tail file is updated successfully*/
	if( RED_FILE_ERR == tail_file_handle ) {
		*err = LOGGER_NVMEM_ERR;
		unlock_mutex(*self->sync_mutex);
		return GET_NULL_FILE;
	}
	*err = LOGGER_OK;
	unlock_mutex( *self->sync_mutex );
	return tail_file_handle;
}

/*pop a filename from log file that alreadly existed in*/
logger_error_t logger_pop( logger_t* self, char* popped_file_name )
{
	DEV_ASSERT( self );

	logger_error_t 	lerr;
	char*			tail_file_name;
	char const* 	head_file_name;
	//uint32_t		fs_err;
	int32_t			tail_file_handle;

	lock_mutex( *self->sync_mutex);

	/* Get the TAIL file. */
	tail_file_name = logger_get_tail(self, &lerr);
	if( lerr != LOGGER_OK ) {
		unlock_mutex( *self->sync_mutex );
		return lerr;
	}

	/* Check if this file exists, if not, we have to update the TAIL. */
	tail_file_handle = red_open(tail_file_name, RED_O_RDWR);
	if( NULL == tail_file_handle ) {
		/* File doesn't exist, so update TAIL. */
		lerr = logger_update_tail(self);
		if( lerr != LOGGER_OK ) {
			unlock_mutex( *self->sync_mutex );
			return lerr;
		}
		/* Get name of TAIL. */
		tail_file_name = logger_get_tail(self, &lerr);
		if( lerr != LOGGER_OK ) {
			unlock_mutex( *self->sync_mutex );
			return lerr;
		}
	} 
	// else if( fs_err != FS_OK ) {
	// 	/* Failed to check for file existance. */
	// 	//unlock_mutex( *self->sync_mutex );
	// 	return LOGGER_NVMEM_ERR;
	// }

	/* Check if this is the HEAD file. If it is, don't touch it. */
	head_file_name = logger_get_head(self, &lerr);
	if( lerr != LOGGER_OK ) {
		unlock_mutex( *self->sync_mutex );
		return lerr;
	}
	if( strncmp(head_file_name, tail_file_name, FILESYSTEM_MAX_NAME_LENGTH) == 0 ) {
		/* HEAD == TAIL, don't untrack head.. */
		unlock_mutex( *self->sync_mutex );
		return LOGGER_EMPTY;
	}

	/* We're removing this file from the ring buffer tracking, so untrack the file. */
	/* This operation just renames it. */
	printf("FILE READY FOR BEING POPOED(BEFORE RENAMED): %s\n",tail_file_name);
	lerr = logger_untrack_file(self, tail_file_name);
	if( lerr != LOGGER_OK ) {
		/* Failed to untrack the file. */
		unlock_mutex( *self->sync_mutex );
		return lerr;
	}

	/* Got the file that is going to be removed, copy it into input buffer. */
	 if( popped_file_name != NULL ) {

		/* Copy name of popped file into here. */
		strncpy(popped_file_name, tail_file_name, FILESYSTEM_MAX_NAME_LENGTH);
		popped_file_name[FILESYSTEM_MAX_NAME_LENGTH] = '\0';
	} 

	/* Update the TAIL. */
	lerr = logger_update_tail(self);
	
	unlock_mutex( *self->sync_mutex );
	return lerr;
}

void logger_task(){

    logger_t self;
    logger_error_t lerr,err;
    int32_t rederr;
    bool_t logger_is_init = MUTEX_FALSE;

    char control_file_name[] = "a.log";
    char element_file_name = 100;
    size_t max_capacity = 100;

    char popped_file_name[13] = "00110001.log";
    popped_file_name[12] = "\0";

    char file_to_insert_name[13] = "00020001.log";
    file_to_insert_name[12] = "\0";

    const char contrl_file_path[] = "a.log";//VOL0:/
    const char popped_file_path[] = "00010000.log";
    const char insert_file_path[] = "00020001.log";

    rederr  = red_open(contrl_file_path, RED_O_RDWR);
    if (rederr == -1) {
        rederr = red_open(contrl_file_path, RED_O_CREAT | RED_O_RDWR);
        if (rederr == -1) {
            exit(red_errno);
        }
        else{
            red_close(rederr);
        }
    }
    rederr  = red_open(popped_file_path, RED_O_RDWR);
    if (rederr == -1) {
        rederr = red_open(popped_file_path, RED_O_CREAT | RED_O_RDWR);
        if (rederr == -1) {
            exit(red_errno);
        }
        else{
            red_close(rederr);
       }
    }
    rederr  = red_open(insert_file_path, RED_O_RDWR);
    if (rederr == -1) {
        rederr = red_open(insert_file_path, RED_O_CREAT | RED_O_RDWR);
        if (rederr == -1) {
            exit(red_errno);
        }
        else{
            red_close(rederr);
        }
    }

    //test demo start here, modify it as wish
    if(initialize_logger(&self, control_file_name, element_file_name, max_capacity, logger_is_init) == LOGGER_OK){
        ex2_log("INITIALIZED OK!\n");
        ex2_log("INIT HEAD NAME: %s\nINIT TAIL NAME: %s\n\n", self.head_file_name, self.tail_file_name);
    }
    else{
        ex2_log("LOGGER FAILED, ERROR CODE: %d\n", (int)lerr);
    }

   if(logger_insert(&self, &err, file_to_insert_name) == NULL)
       ex2_log("NULL FILE\n");
    else
    {
        ex2_log("INSERTED OK!\n");
        ex2_log("CURRENT HEAD NAME: %s\nCURRENT TAIL NAME: %s\n\n", self.head_file_name, self.tail_file_name);
    }

    if(logger_pop(&self, popped_file_name) != LOGGER_OK){
        ex2_log("POP FAILED\n");
    }
    else
    {
        ex2_log("POPPED FILE NAME: %s\n", popped_file_name);
        ex2_log("CURRENT HEAD NAME: %s\nCURRENT TAIL NAME: %s\n\n", self.head_file_name, self.tail_file_name);
    }

    logger_peek_tail(&self, &err);
    if(err != LOGGER_OK)
        ex2_log("PEEK TAIL FAILED\n");
    else
    {
        ex2_log("PEEK TAIL OK!\n");
        ex2_log("CURRENT HEAD NAME: %s\nCURRENT TAIL NAME: %s\n\n", self.head_file_name, self.tail_file_name);
    }

    logger_peek_head(&self, &err);
    if(err != LOGGER_OK)
        ex2_log("PEEK HEAD FAILED\n");
    else
    {
        ex2_log("PEEK HEAD OK!\n");
        ex2_log("CURRENT HEAD NAME: %s\nCURRENT TAIL NAME: %s\n\n", self.head_file_name, self.tail_file_name);
    }

}

SAT_returnState start_logger_task(void) {
    if (xTaskCreate((TaskFunction_t)logger_task,
                  "logger system", 2048, NULL, LOGGER_TASK_PRIO,
                  NULL) != pdPASS) {
        ex2_log("FAILED TO CREATE TASK logger\n");
        return SATR_ERROR;
    }
    ex2_log("logger system started\n");
    return SATR_OK;
}
