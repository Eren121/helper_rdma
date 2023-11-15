SERVER_ADDR = 192.168.0.2
SERVER_PORT = 44564
CLIENT_COMMAND = cd /home/raphael/infiniband/builddir && ./infiniband -c $(SERVER_ADDR) $(SERVER_PORT)
CLIENT_HOSTNAME = attila
SERVER_CMD_PREFIX = 

# Run both a client and a server
# Assumes a tmux session with ghengis on pane 0 and attila on pane 1, currently focusing pane 0
# Assumes the meson build directory is named builddir
.PHONY: run
run:
	# Stops the client on attila if it is running
	tmux send-keys -t 1 C-c

	# Build
	cd builddir && ninja

	# Copy executable to attila
	scp builddir/infiniband $(CLIENT_HOSTNAME):/home/raphael/infiniband/builddir

	# Run client after some delay
	sleep 0.4s && tmux send-keys -t 1 "$(CLIENT_COMMAND)" Enter &

	# Run server immediately
	$(SERVER_CMD_PREFIX) ./builddir/infiniband -s $(SERVER_ADDR) $(SERVER_PORT)

.PHONY: gdb
gdb-server: run
gdb-server: SERVER_CMD_PREFIX = gdb -ex run --args