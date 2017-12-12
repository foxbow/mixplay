from flask import Flask
from flask import request
import sys
import socket
import threading
import time

# the socket to mixplayd
s = None
app = Flask(__name__, static_folder='./' )

@app.route('/')
@app.route('/index.html')
def staticFiles():
    return app.send_static_file('mixplay.html')
        
@app.route('/mixplay.js')
def js():
    return app.send_static_file('mixplay.js')
        
@app.route('/mixplay.css')
def css():
    return app.send_static_file('mixplay.css')
        
@app.route( "/cmd/<cmd>" )
def sendCMD( cmd ):
	if( cmd=="mpc_quit" ):
		if( lissy.isAlive() ):
			lissy.stop();
		func = request.environ.get('werkzeug.server.shutdown')
		if func is None:
			raise RuntimeError('Not running with the Werkzeug Server')
		func()
		return "ok", 200
	elif( cmd=="mpc_start" ):
		lissy.start()
		return "restarting", 504
	elif( lissy.isAlive() ):
		s.sendall( cmd );
		return "ok", 200
	else:
		return 503

@app.route( "/status" )
def sendStatus():
	if( lissy.isAlive() ):
		return lissy.data
	else:
		return "fail", 503

class listener( threading.Thread ):
	def __init__( self, socket ):
		threading.Thread.__init__(self)
		self.socket=socket
		self.data="{}"
		self.running=1
		self.socket=socket

	def run(self):
		while self.running==1:
			self.data=self.socket.recv( 2048 )
			if not self.data:
				self.running=0
		print "Connection broken"
	
	def stop(self):
		self.running=0;
		
if __name__ == '__main__':
	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	s.connect(( 'localhost', 2347 ))
	lissy=listener( s )
	lissy.start()
#	app.debug = True
	app.run( host='0.0.0.0', port=8080 )
	lissy.stop()
    
