# Static files for the web UI
These files contain all the static content to set up and run the UI in a browser. If someone runs mixplay through a real webserver like nginx, they should add a rule that only requests for /mpctrl\* are forwarded to mixplayd, the rest can be served as files from here.

Caveat:
mixplay.html, mixplay.jss and mixplay.css are generated during build and no longer part of the repository!
