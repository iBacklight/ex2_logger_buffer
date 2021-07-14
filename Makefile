# **************** Ptoject DIRs *********************
# PROJDIR = $(CURDIR)/../ex2_obc_software
PROJDIR = $(CURDIR)/../SatelliteSim
SRC_DIRS = $(CURDIR)/source
SRC_DIRS = $(CURDIR)/source
RTOS_DIRS = $(CURDIR)/../ex2_obc_software/source

# **************** Includes Free RTOS intergration ************
INCLUDE += -I $(CURDIR)/include
#path to source includes
INCLUDE += -I$(PROJDIR)/Source/include
#path to compiler includes (portmacro.h)
INCLUDE += -I$(PROJDIR)/Source/portable/GCC/POSIX
#path to FreeRTOSconfig
INCLUDE += -I$(PROJDIR)/Project
# INCLUDE += -I $(PROJDIR)/include/ex2_os
# INCLUDE += -I $(PROJDIR)/include

# **************** Cflags ****************************
CC = gcc
CFLAGS += -lpthread -lrc -std=c99
CFLAGS += $(INCLUDE)

# **************** Cfiles ****************************
CFILES += $(SRC_DIRS)/main.c
CFILES += $(SRC_DIRS)/logger.c
CFILES += $(PROJDIR)/Source/portable/GCC/POSIX/port.c
CFILES += $(PROJDIR)/Source/*.c
# CFILES += $(RTOS_DIRS)/os_queue.c
# CFILES += $(RTOS_DIRS)/os_tasks.c
# CFILES += $(RTOS_DIRS)/os_timer.c


# **************** Configs ***************************
TAR = $(CURDIR)/logger
OBJ = $(patsubst %.c, %.o, $(CFILES))
RM := rm
# SRC = main.c logger.c os_queue.c os_tasks.c os_timer.c
# OBJ = main.o logger.o os_queue.o os_tasks.o os_timers.o 
# DEP = logger.h main.h

# VPATH += $(SRC_DIRS)
# VPATH += $(RTOS_DIRS)
# VPATH += $(INC_DIRS)

# INC_FLAGS := $(addprefix -I , $(INC_DIRS))

all:$(TAR)

$(TAR):$(OBJ)
	$(CC) $(OBJ) -o $(TAR)
	# $(CC) $^ -o $@ 

# %.o:%.c
# 	$(CC) -c %.c -o %.o

# main.o:main.c
# 	$(CC) -c $(SRC_DIRS)/main.c $(INC_FLAGS) -o main.o 


# logger_test.o:logger_test.c
# 	$(CC) -c $(SRC_DIRS)/logger.c  -o logger.o $(INC_FLAGS)

# queue.o:queue.c
# 	$(CC) -c $(RTOS_DIRS)/os_queue.c $(INC_FLAGS) -o os_queue.o

# tasks.o:tasks.c
# 	$(CC) -c $(RTOS_DIRS)/os_tasks.c $(INC_FLAGS) -o os_tasks.o

# timers.o:timers.c
# 	$(CC) -c $(RTOS_DIRS)/os_timers.c $(INC_FLAGS) -o os_timers.o


.PHONY:clean
clean:
	$(RM) -rf $(TAR) $(OBJ)

