To run a minimal X with a browser to display the UI on a touchscreen install the following packages:

# sudo apt-get --no-install-recommends install xserver-xorg xserver-xorg-video-fbdev xinit pciutils xinput xfonts-100dpi xfonts-75dpi xfonts-scalable chromium-browser

Start the browser (wip)
# startx /usr/bin/chromium-browser --app="http://localhost:2347/"
