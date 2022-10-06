Headless support
=

Once mixplayd runs and plays as it should, it may be interesting to turn the box into a standalone player

**To run a minimal X with a browser to display the UI on a touchscreen install the following packages:**

\# sudo apt --no-install-recommends install xserver-xorg xserver-xorg-video-fbdev xinit xinput xfonts-100dpi xfonts-75dpi xfonts-scalable chromium-browser

**Start X with the browser**

startx /usr/bin/chromium-browser --disable-plugins --window-position=0,0 --window-size=800,480 --app=http://localhost:2347/ -- -nocursor -s 0

**Autostart mixplayd with the browser interface in kiosk mode**

* Enable auto log-in of the desired user.
* Add a runall.sh script
```
#!/bin/bash
mixplay/bin/mixplayd
mixplay/bin/mixplay-scr
startx /usr/bin/chromium-browser --disable-plugins --window-position=0,0 --window-size=800,480 --app=http://localhost:2347/ -- -nocursor -s 0
# uncomment if you want an autoreboot after a crash..
# sudo reboot
```
* add this to the user's .profile
```
# start mixplay when on console, otherwise just end up on the prompt
if [ -z "$SSH_CLIENT" ] && [ -z "$SSH_TTY" ]; then
	${HOME}/runall.sh
fi
```
* reboot and hope for the best
