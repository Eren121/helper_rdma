#!/bin/bash
#
# Run bandwidth test.
# Assumes a tmux session inside ghengis with ghengis focused on pane 0 and attila on pane 1.

iperf3 -s 192.168.0.2 &
tmux send-keys -t 1 'iperf3 -c 192.168.0.2' Enter

fg
