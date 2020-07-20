CC = gcc
CCFLAGS += -std=c11 -Werror -Wall -pthread

#================
# ADMINISTRATION
#================

install :
	@touch /var/tmp/nbd-server-keyseed-file
	@mkdir bin data-to-backup storage
	@printf "\033[1;33mInstallation complete!\033[0m\n"

clean :
	@rm -f /var/tmp/nbd-server-keyseed-file
	@rm -rf bin data-to-backup storage
	@rm -rf serverside-fs clientside-fs
	@printf "\033[1;33mCleaning complete!\033[0m\n"

add-manpages : liburing-manpages/*
	@cp liburing-manpages/* /usr/share/man/man2
	@printf "\033[1;33mAdded io_uring_setup, io_uring_enter, io_uring_register manpages to /usr/share/man/man2\033[0m\n"

#=============
# COMPILATION 
#=============

HEADERS = src/NBD.h src/Logging.h src/Connection.h src/Negotiation.h src/OptionHaggling.h src/IO_Ring.h src/IO_Request.h src/NBD_Request.h src/Transmission.h

bin/nbd-server : src/nbd-server.c ${HEADERS}
	${CC} ${CCFLAGS} $< -o $@

bin/kill-after : test/kill-after.c
	${CC} ${CCFLAGS} $< -o $@

bin/execute-after : test/execute-after.c
	${CC} ${CCFLAGS} $< -o $@

compile : bin/nbd-server

#=========
# TESTING
#=========

run_backup_server : bin/nbd-server
	@printf "\033[1;33mRunning server!\033[0m\n"
	@rm -rf serverside-fs
	@dd if=/dev/zero of=serverside-fs bs=1024 count=36000
	@mkfs.ext4 serverside-fs -d data-to-backup
	@bin/nbd-server serverside-fs
	@rm -rf serverside-fs

run_linux_client:
	@printf "\033[1;33mRunning linux-client!\033[0m\n"
	@sudo modprobe nbd
	@sudo nbd-client localhost /dev/nbd0
	@printf "\033[1;33mMounting the backup fs!\033[0m\n"
	@sudo mount /dev/nbd0 storage

run_qemu_client:
	@printf "\033[1;33mRunning qemu-client!\033[0m\n"
	@sudo modprobe nbd
	@sudo qemu-nbd --read-only --connect=/dev/nbd0 nbd:localhost:10809 --aio=native
	@printf "\033[1;33mMounting the backup fs!\033[0m\n"
	@sudo mount -t ext4 /dev/nbd0 storage

run_plain_mount:
	@printf "\033[1;33mMounting the backup fs!\033[0m\n"
	@sudo mount -t ext4 serverside-fs storage


test_connection_hangup : bin/execute-after
	@printf "\033[1;33mRunning connection test with qemu-client\033[0m\n"
	@sudo modprobe nbd
	@sudo bin/execute-after 50 ifconfig lo down
	@bin/kill-after 6000 qemu-nbd --read-only --connect=/dev/nbd0 nbd:localhost:10809 --aio=native
	@sudo ifconfig lo up

stop_backup:
	@sudo nbd-client -disconnect /dev/nbd0
	@sudo qemu-nbd --disconnect /dev/nbd0
	@sudo umount /dev/nbd0

.PHONY: install clean add-manpages compile run_backup_server run_linux_client run_qemu_client stop_backup
