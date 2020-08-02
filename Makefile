CC = gcc
CCFLAGS += -std=c11 -Werror -Wall -O3 -pthread

#================
# ADMINISTRATION
#================

default : directories compile
	@printf "\033[1;33mInstallation complete!\033[0m\n"

directories:
	@mkdir -p bin data-to-backup
	@printf "\033[1;33mDirectories created!\033[0m\n"

clean:
	@rm -rf bin serverside-mount clientside-mount
	@rm -rf serverside-fs
	@printf "\033[1;33mCleaning complete!\033[0m\n"

add-manpages : liburing-manpages/*
	@cp liburing-manpages/* /usr/share/man/man2
	@printf "\033[1;33mAdded io_uring_setup, io_uring_enter, io_uring_register manpages to /usr/share/man/man2\033[0m\n"

#=============
# COMPILATION 
#=============

HEADERS = src/NBD.h src/Logging.h src/Connection.h src/Negotiation.h src/OptionHaggling.h \
          src/IO_Ring.h src/IO_Request.h src/NBD_Request.h src/Transmission.h

bin/nbd-server : src/nbd-server.c ${HEADERS}
	${CC} ${CCFLAGS} $< -o $@

bin/kill-after : test/kill-after.c
	${CC} ${CCFLAGS} $< -o $@

bin/execute-after : test/execute-after.c
	${CC} ${CCFLAGS} $< -o $@

compile : bin/nbd-server bin/kill-after bin/execute-after
	@printf "\033[1;33mBinaries compiled!\033[0m\n"

#=========
# TESTING
#=========

BLOCK_SIZE=4K
NUM_BLOCKS=1M

# Serverside FS

create-serverside-fs:
	@printf "\033[1;33mCreating the serverside fs\033[0m\n"
	@rm -f serverside-fs
	@dd if=/dev/zero of=serverside-fs bs=1K count=5M
	@mkfs.ext4 serverside-fs -d data-to-backup

mount-serverside-fs:
	@printf "\033[1;33mMounting the serverside fs\033[0m\n"
	@mkdir -p serverside-mount
	@sudo mount -t ext4 serverside-fs serverside-mount

write-data-to-serverside-fs:
	@printf "\033[1;33mCreating the performance-test-file\033[0m\n"
	@sudo dd if=/dev/zero of=serverside-mount/performance-test-file bs=${BLOCK_SIZE} count=${NUM_BLOCKS}

umount-serverside-fs:
	@printf "\033[1;33mUnmounting the serverside fs\033[0m\n"
	@sudo umount serverside-mount
	@rm -rf serverside-mount

# Clientside FS

mount-clientside-fs:
	@printf "\033[1;33mMounting the clientside fs\033[0m\n"
	@mkdir -p clientside-mount
	@sudo mount -t ext4 /dev/nbd0 clientside-mount

write-data-to-clientside-fs:
	@printf "\033[1;33mCreating the performance-test-file\033[0m\n"
	@sudo dd if=/dev/zero of=clientside-mount/performance-test-file bs=${BLOCK_SIZE} count=${NUM_BLOCKS}

umount-clientside-fs: 
	@printf "\033[1;33mUnmounting the clientside fs\033[0m\n"
	@sudo umount /dev/nbd0
	@rm -rf clientside-mount

# Data Transfer

run-backup-server : bin/nbd-server
	@printf "\033[1;33mRunning server\033[0m\n"
	@bin/nbd-server serverside-fs

run-linux-client:
	@printf "\033[1;33mRunning linux-client\033[0m\n"
	@sudo modprobe nbd
	@sudo nbd-client localhost /dev/nbd0

run-qemu-client:
	@printf "\033[1;33mRunning qemu-client\033[0m\n"
	@sudo modprobe nbd
	@sudo qemu-nbd --connect=/dev/nbd0 nbd:localhost:10809 --aio=native --format=raw

stop-backup:
	@sudo nbd-client -disconnect /dev/nbd0
	@sudo qemu-nbd --disconnect /dev/nbd0
	@printf "\033[1;33mBackup session finished\033[0m\n"

# Connection Hangup Test (assuming both client and server to be on one machine)

test-connection-hangup : bin/kill-after bin/execute-after
	@printf "\033[1;33mRunning connection hangup test with qemu-client\033[0m\n"
	@sudo modprobe nbd
	@sudo bin/execute-after 50 ifconfig lo down
	@sudo bin/kill-after 6000 qemu-nbd --connect=/dev/nbd0 nbd:localhost:10809 --aio=native --format=raw
	@sudo ifconfig lo up

.PHONY: install clean add-manpages compile                                                        \
        create-serverside-fs mount-serverside-fs write-data-to-serverside-fs umount-serverside-fs \
        mount-clientside-fs write-data-to-clientside-fs umount-clientside-fs                      \
        run-backup-server run-linux-client run-qemu-client stop-backup                            \
        test-connection-hangup
