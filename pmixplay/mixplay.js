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
  		if ( xmlhttp.readyState==4 && xmlhttp.status!=200 ) {
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
	  				if( data.version != 5 ) {
	  					alert("Version clash, expected 5 and got "+data.version );
	  					return;
	  				}
	  				document.title=data.current.artist+" - "+data.current.title;
		  			setElement( 'prev', data.prev.artist+" - "+data.prev.title );
		  			setElement( 'artist', data.current.artist );
		  			setElement( 'title', data.current.title );
		  			setElement( 'album', data.current.album );
		  			setElement( 'next', data.next.artist+" - "+data.next.title );
		  			setElement( 'playtime', data.playtime+" / "+data.remtime );
		  			setElement( 'volume', data.volume+"%" );
		  			if( data.msg != "" ) {
		  				alert( data.msg );
		  			}
		  			if( data.flags & 1 )  {
			  			document.getElementById( 'fav' ).disabled=false;
		  			}
		  			else {
			  			document.getElementById( 'fav' ).disabled=true;		  			
		  			}
		  		}
		  	}
		  	else if( xmlhttp.status==503 ) {
		  		if( confirm( "Try to restart thread?" ) ) {
		  			sendCMD( "mpc_start" );
		  		}
		  		else{
		  			alert( "Stopping update.." );
		  			return;
		  		}
		  	}
		}
	}
	
	xmlhttp.open("GET", "/status", true);
	xmlhttp.send();
	setTimeout("updateUI()",1000)
}

updateUI();

