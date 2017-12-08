from flask import Flask
from flask import request
import sys
import socket

# the socket to mixplayd
s = None

class Title:
	def __init__( self, artist, album, title, flags ):
		self.artist=artist
		self.album=album
		self.title=title
		self.flags=flags
		
class CommInfo:
	def __init__( self, packed ):
		
		
app = Flask(__name__)
        
@app.route( "/cmd/<str:cmd>" )
def sendCMD( cmd ):
	s.sendall( cmd )
    return "ok"

@app.route( "/status" )
def sendStatus():
	data=s.recv(1024)
	if not data:
	    s.close()
	    return "exit"
	    app.stop()
	return CommInfo( data ).toXML()
	
if __name__ == '__main__':
	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	s.connect(( 'localhost', 2347))
    app.debug = True
    app.run()

