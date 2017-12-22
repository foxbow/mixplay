doUpdate=1;
data=null;
mpver=7;

function playPause() {
	if( data != null ) {
		sendCMD( "mpc_play" );
		if( data.status == 0 ) {
			if( confirm( "Replay?" ) ) {
				sendCMD( "mpc_repl" )
			}
			sendCMD( "mpc_play" )
		}
	}
}

/**
 * used cmd's so far are:
 *  mpc_play,
 *  mpc_prev,
 *  mpc_next,
 *  mpc_favtitle,
 *  mpc_repl,
 *  mpc_dnptitle,
 *	mpc_ivol,
 *	mpc_dvol,
 *  mpc_quit - just terminate pmixplay
 */
function sendCMD( cmd ) {
	var xmlhttp=new XMLHttpRequest();
	xmlhttp.onreadystatechange=function() {
  		if ( xmlhttp.readyState==4 && xmlhttp.status!=204 ) {
			alert( "Error "+xmlhttp.status );
  		}
	}
	
	xmlhttp.open("GET", "/cmd/"+cmd, true);
	xmlhttp.send();
}

function setElement( e, val ) {
	document.getElementById( e ).innerHTML=val;
}

function updateUI( ){
	var xmlhttp=new XMLHttpRequest();
	xmlhttp.onreadystatechange=function() {
  		if (xmlhttp.readyState==4 ) {
  			if( xmlhttp.status==200 ) {
	  			data=JSON.parse(xmlhttp.responseText);
	  			if( data !== undefined ) {
	  				if( data.version != mpver ) {
	  					doUpdate=0;
	  					alert("Version clash, expected "+mpver+" and got "+data.version );
	  					return;
	  				}
	  				document.title=data.current.artist+" - "+data.current.title;
		  			setElement( 'prev', data.prev.artist+" - "+data.prev.title );
		  			setElement( 'artist', data.current.artist );
		  			if( data.status == 0 ) {
			  			setElement( 'title', data.current.title );
			  		}
			  		else {
			  			setElement( 'title', "<i>"+data.current.title+"</i>" );
			  		}	
		  			setElement( 'album', data.current.album );
		  			setElement( 'next', data.next.artist+" - "+data.next.title );
		  			setElement( 'playtime', data.playtime+" / "+data.remtime );
		  			setElement( 'volume', data.volume+"%" );
//		  			if( data.msg != "" ) {
//		  				alert( data.msg );
//		  			}
		  			if( data.current.flags & 1 )  {
			  			document.getElementById( 'fav' ).disabled=true;
		  			}
		  			else {
			  			document.getElementById( 'fav' ).disabled=false;		  			
		  			}
		  		}
		  	}
		  	else if( xmlhttp.status==503 ) {
		  		if( confirm( "Try to restart?" ) ) {
		  			sendCMD( "mpc_start" );
		  		}
		  		else{
		  			alert( "Stopping updates.." );
		  			doUpdate=0;
		  		}
		  	}
		}
	}
	
	if( doUpdate != 0 ) {
		xmlhttp.open("GET", "/status", true);
		xmlhttp.send();
		setTimeout("updateUI()",500);
	}
}

updateUI();

