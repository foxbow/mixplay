#!/bin/bash
valgrind --show-leak-kinds=definite,possible,indirect --leak-check=full --suppressions=bin/mixplayd.supp $* ./bin/mixplayd -d
