#!/bin/bash
echo "Program FLIRC"
echo ""
echo "Use your remote to train the FLIRC adapter on unknown remotes"
echo ""
echo "Do NOT use this if the FLIRC adapter already 'knows' the remote"
echo "eg when it's already used for KODI or other programs!"
echo ""
echo "Certain remotes send weird codes so each action will be checked twice"
echo "Ignore alternating 'button already exists' messages!"
echo ""
echo "Skip buttons with CTRL-C"
echo ""

if [ "$1" != "" ]; then
  FLIRCUTIL=$1;
fi

FLIRCUTIL=${FLIRCUTIL}flirc_util

# Check if ${FLIRCUTIL} is available
if [ "$(which ${FLIRCUTIL})" == "" ]; then
  echo "${FLIRCUTIL} not found!"
  echo "Restart as: $0 <path_to_flirc_util>"
  exit
fi

# Start training
echo "Play/Pause"
sudo ${FLIRCUTIL} record play/pause
sudo ${FLIRCUTIL} record play/pause
echo "Prev"
sudo ${FLIRCUTIL} record prev_track
sudo ${FLIRCUTIL} record prev_track
echo "Next"
sudo ${FLIRCUTIL} record next_track
sudo ${FLIRCUTIL} record next_track
echo "Favoutite"
sudo ${FLIRCUTIL} record f
sudo ${FLIRCUTIL} record f
echo "Do Not Play"
sudo ${FLIRCUTIL} record d
sudo ${FLIRCUTIL} record d
echo "Mute"
sudo ${FLIRCUTIL} record mute
sudo ${FLIRCUTIL} record mute
echo "Vol+"
sudo ${FLIRCUTIL} record vol_up
sudo ${FLIRCUTIL} record vol_up
echo "Vol-"
sudo ${FLIRCUTIL} record vol_down
sudo ${FLIRCUTIL} record vol_down

echo ""
echo "Done."
