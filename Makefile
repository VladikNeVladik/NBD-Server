CC = gcc
CCFLAGS += -std=c11 -Werror -Wall -pthread

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

HEADERS = src/NBD.h src/Logging.h src/Negotiation.h src/OptionHaggling.h src/IO_Ring.h src/RequestManagement.h src/Transmission.h

bin/nbd-server : src/nbd-server.c ${HEADERS}
	${CC} ${CCFLAGS} $< -o $@

compile : bin/nbd-server

run_backup_server : compile
	@printf "\033[1;33mRunning server!\033[0m\n"
	@rm -rf serverside-fs
	@dd if=/dev/zero of=serverside-fs bs=1024 count=1000
	@mkfs.ext4 serverside-fs -d data-to-backup
	@#strace -f bin/nbd-server serverside/nbd-fs
	@bin/nbd-server serverside-fs
	@rm -rf serverside-fs

linux_client:
	@printf "\033[1;33mDon't forget to \'modprobe nbd\'\033[0m\n"
	@printf "\033[1;33mRunning linux-client!\033[0m\n"
	@nbd-client localhost /dev/nbd0
	@printf "\033[1;33mMounting the backup fs!\033[0m\n"
	@mount /dev/nbd0 storage

stop_backup:
	@nbd-client -disconnect /dev/nbd0
	@umount /dev/nbd0

.PHONY: install clean add-manpages compile run_backup_server linux_client qemu_client stop_backup
