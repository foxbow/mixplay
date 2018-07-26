#!/bin/bash
valgrind --show-leak-kinds=definite,possible,indirect --leak-check=full bin/mixplayd -d
